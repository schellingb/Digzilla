/*
  Digzilla
  Copyright (C) 2021 Bernhard Schelling

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <ZL_Application.h>
#include <ZL_Display.h>
#include <ZL_Surface.h>
#include <ZL_Signal.h>
#include <ZL_Audio.h>
#include <ZL_Font.h>
#include <ZL_Scene.h>
#include <ZL_Input.h>
#include <ZL_SynthImc.h>
#include <vector>

static ZL_Font fntTitle, fntBig, fntBigOutline;
static ZL_Surface srfPlayer;
static ZL_Surface srfTiles;

#define PLAYER_HALFWIDTH .25f
#define PLAYER_HALFHEIGHT .3f
#define WORLD_WIDTH 32

#define TOMTPF (1.f/60.f)
#define TOMELAPSEDF(factor) (TOMTPF*(s(factor)))

enum
{
	TILE_STREET,
	_TILE_FIRST_DIGGABLE,
	TILE_DIRT,
	TILE_ROCK_STUCK,
	TILE_DIAMOND_STUCK,
	TILE_GOLD_STUCK,
	TILE_SILVER_STUCK,
	TILE_BRONZE_STUCK,
	TILE_GRAIL_STUCK,
	_TILE_LAST_DIGGABLE,

	_TILE_FIRST_DIGGED_OUT,
	TILE_ROCK_FREE,

	_TILE_FIRST_NOCOLLISION,
	TILE_DIAMOND_FREE,
	TILE_GOLD_FREE,
	TILE_SILVER_FREE,
	TILE_BRONZE_FREE,
	TILE_GRAIL_FREE,
	_TILE_LAST_DIGGED_OUT,
	TILE_EMPTY,


	GFX_PLAYER_FACE = 0,
	GFX_PLAYER_HAT,
	GFX_PLAYER_LEG_STAND = 3,
	GFX_PLAYER_LEG_JUMP,
	GFX_PLAYER_LEG_WALK1,
	GFX_PLAYER_LEG_WALK2,
	GFX_PLAYER_LEG_WALK3,
	GFX_PLAYER_TORSO_HOLD,
	GFX_PLAYER_TORSO_HIT1,
	GFX_PLAYER_TORSO_HIT2,
	GFX_PLAYER_TORSO_HIT3,
	GFX_PICKAXE_HOLD,
	GFX_PICKAXE_HIT1,
	GFX_PICKAXE_HIT2,
	GFX_PICKAXE_HIT3,

	GFX_TILE_STREET = 16,
	GFX_TILE_DIRT = 20,
	GFX_TILE_EMPTY = 19,
	GFX_TILE_SHAFT = 24,
	GFX_TILE_ELEVATOR = 25,
	GFX_TILE_ROCK = 26,
	GFX_TILE_ORE_STUCK = 27,
	GFX_TILE_ORE_FREE = 28,
	GFX_SHOP1 = 4*8,
	GFX_HOSPITAL1 = 4*8+3,
	GFX_HOME1 = 4*8+6,
	GFX_HUD_MONEY = 7*8,
	GFX_HUD_TNT,
	GFX_ITEM_TNT,
	GFX_EXPLOSION,
	GFX_TILE_GRAIL,
};

static const ZL_Color ColorBronze  (0.80f, 0.40f, 0.10f, 1.f);
static const ZL_Color ColorSilver  (0.70f, 0.70f, 0.70f, 1.f);
static const ZL_Color ColorGold    (1.00f, 1.00f, 0.20f, 1.f);
static const ZL_Color ColorDiamond (0.40f, 0.80f, 1.00f, 1.f);

#define PLAYER_START_X 7.5f
#define HOSPITAL_DOOR_X 15.5f

static ZL_Sound sndJump, sndCollect, sndDig, sndPush, sndBigDrop, sndDrop, sndDeath, sndRespawn, sndMenu, sndExplosion;
static ZL_SynthImcTrack imcElevator, imcWin, imcMusic;

struct Falling
{
	Falling(float x, float y, unsigned char tile) : x(x), y(y), vely(0), tile(tile) {}
	float x, y, vely;
	unsigned char tile;
};

struct TNT
{
	TNT(int ti) : ti(ti), timer(0) {}
	int ti, timer;
};

struct Player
{
	Player()
	{
		memset(this, 0, sizeof(*this));
		static const ZL_Color skinTones[] = { ZLRGBFF(237,192,161), ZLRGBFF(211,142,111), ZLRGBFF(234,183,138), ZLRGBFF(197,132,92), ZLRGBFF(88,59,43) };
		colorHead = RAND_ARRAYELEMENT(skinTones);
		Dress();
	}
	void Dress()
	{
		colorHat = RAND_COLOR;
		colorTorso = RAND_COLOR;
		colorLegs = RAND_COLOR;
	}
	float x, y, oldy;
	float movex, movey, velx, vely;
	bool grounded, dead, lookLeft;
	int attack, jump, score, deaths;
	ticks_t groundedTicks;
	ZL_Color colorHead, colorHat, colorTorso, colorLegs;
	//ZL_JoystickData* joy;
	float AxisX() { return (ZL_Input::Held(ZLK_A) || ZL_Input::Held(ZLK_LEFT) ? -1.f : 0.f) + (ZL_Input::Held(ZLK_D) || ZL_Input::Held(ZLK_RIGHT) ? 1.f : 0.f); }
	float AxisY() { return (ZL_Input::Held(ZLK_S) || ZL_Input::Held(ZLK_DOWN) ? -1.f : 0.f) + (ZL_Input::Held(ZLK_W) || ZL_Input::Held(ZLK_UP)   ?  1.f : 0.f); }
	int PressUpDown() { return (ZL_Input::Down(ZLK_S) || ZL_Input::Down(ZLK_DOWN) ? -1 : 0) + (ZL_Input::Down(ZLK_W) || ZL_Input::Down(ZLK_UP)   ?  1 : 0); }
	bool ButtonAttack() { return !!ZL_Input::Held(ZL_BUTTON_LEFT) || ZL_Input::Held(ZLK_LCTRL) || ZL_Input::Held(ZLK_RCTRL); }
	bool ButtonJump() { return !!ZL_Input::Held(ZL_BUTTON_RIGHT) || ZL_Input::Held(ZLK_SPACE) || ZL_Input::Held(ZLK_LALT) || ZL_Input::Held(ZLK_RALT);  }
	bool PressAny() { return !!ZL_Input::Down(ZL_BUTTON_LEFT, true) || ZL_Input::Down(ZLK_LCTRL, true) || ZL_Input::Down(ZLK_SPACE, true); }
};

struct Collected { int ti; unsigned char tile; };

static bool IsTitle = true;
static float ScreenTop;
static int Money;
static int Tnt;
static std::vector<Player> players;
static std::vector<Falling> fallings;
static std::vector<TNT> tnts;
static std::vector<ZL_Vector> colsv, colsh;
static std::vector<Collected> collecteds;
static bool InShop;
static int ShopCursor;
static float elevatorY;
static float elevatorVel;
static int ElevatorDepth;
static ticks_t WinTicks;
static ZL_SeededRand rnd;
static std::vector<unsigned char> MapStorage;
static unsigned char* Map;
static void AddMapRows(int max_depth);
static void Empty(int ti, bool destroyStuck = false);

static void Reset()
{
	rnd = ZL_SeededRand();
	ScreenTop = -10;
	Money = 0;
	Tnt = 2;
	players.clear();
	fallings.clear();
	tnts.clear();
	colsv.clear();
	colsh.clear();
	InShop = false;
	ShopCursor = 0;
	elevatorY = 0;
	elevatorVel = 0;
	ElevatorDepth = 10;
	WinTicks = 0;
	MapStorage.clear();
	Map = NULL;
	AddMapRows(50);

	players.push_back(Player());
	players[0].x = PLAYER_START_X;
}

static void AddMapRows(int max_depth)
{
	int cur_rows = (MapStorage.size() ? MapStorage.size() / WORLD_WIDTH - 5 : 0);
	if (cur_rows >= max_depth) return;
	max_depth = 10 + (max_depth + 10) / 10 * 10;
	MapStorage.resize((5 + max_depth) * WORLD_WIDTH);
	Map = &MapStorage[5*WORLD_WIDTH];
	for (int y = cur_rows; y != max_depth; y++)
	{
		float dirtChance    =                ZL_Math::MapClamped((float)y, 0,  100, .9f, 1.4f);
		float rockChance    = dirtChance   + ZL_Math::MapClamped((float)y, 0,  100, .08f, .6f);
		float bronzeChance  = rockChance   + ZL_Math::MapClamped((float)y, 0,  100, .05f, .5f);
		float silverChance  = bronzeChance + (y < 10 ? 0 : ZL_Math::MapClamped((float)y, 10, 100, .06f, .4f));
		float goldChance    = silverChance + (y < 20 ? 0 : ZL_Math::MapClamped((float)y, 20, 100, .05f, .3f));
		float diamondChance = goldChance   + (y < 30 ? 0 : ZL_Math::MapClamped((float)y, 30, 100, .05f, .2f));
		for (int x = 0; x != WORLD_WIDTH-1; x++)
		{
			float r = rnd.RangeEx(0, diamondChance);
			Map[y * WORLD_WIDTH + x] =
				(r < dirtChance    ? TILE_DIRT          :
				(r < rockChance    ? TILE_ROCK_STUCK    :
				(r < bronzeChance  ? TILE_BRONZE_STUCK  :
				(r < silverChance  ? TILE_SILVER_STUCK  :
				(r < goldChance    ? TILE_GOLD_STUCK    :
				                     TILE_DIAMOND_STUCK)))));
		}
		Map[y * WORLD_WIDTH + WORLD_WIDTH - 1] = TILE_DIRT;

		if (y == 50)
		{
			Map[y * WORLD_WIDTH + RAND_INT_RANGE(2, 16)] = TILE_GRAIL_STUCK;
		}
	}
	if (!cur_rows)
	{
		memset(&MapStorage[0], TILE_EMPTY, 5 * WORLD_WIDTH);
		memset(Map, TILE_STREET, WORLD_WIDTH);
		Map[WORLD_WIDTH - 1] = TILE_DIRT;
	}
}

int GetValue(unsigned char tile)
{
	switch (tile)
	{
		case TILE_BRONZE_FREE:  return 10;
		case TILE_SILVER_FREE:  return 15;
		case TILE_GOLD_FREE:    return 30;
		case TILE_DIAMOND_FREE: return 50;
		case TILE_GRAIL_FREE:   return 10000;
		default: ZL_NO_ENTRY(); return 0;
	}
}

static void Collect(Player& player, unsigned char tile, int ti)
{
	int value = GetValue(tile);
	player.score += value;
	Money += value;
	if (tile == TILE_GRAIL_FREE) { imcWin.Play(); WinTicks = ZLTICKS; }
	else sndCollect.Play();
	collecteds.push_back({ti, tile});
}

static void Respawn(Player& player)
{
	player.dead = false;
	player.x = HOSPITAL_DOOR_X;
	player.y = 0.4f;
	player.lookLeft = false;
	player.velx = player.vely = 0;
	player.attack = 8; // hack: avoid immediate new attack swing

	for (int i = collecteds.size() - 1; i >= 0; i--)
	{
		Collected c = collecteds[i];
		if (Map[c.ti] != TILE_EMPTY) continue;
		Map[c.ti] = c.tile;
		Empty(c.ti);
		collecteds.erase(collecteds.begin() + i);
		Money -= GetValue(c.tile);
		if (Money <= 0) break;
	}
	Money = 0;
	sndRespawn.Play();
}

static void PrepareCollision(Player& player)
{
	ZL_Rectf player_rec = ZL_Rectf::FromCenter(player.x, player.y+PLAYER_HALFHEIGHT, PLAYER_HALFWIDTH, PLAYER_HALFHEIGHT);
	ZL_Vector player_center = player_rec.Center();

	colsv.clear();
	colsh.clear();
	int ptx = (int)sfloor(player.x), pty = (int)sfloor(-(player.y+PLAYER_HALFHEIGHT));
	int pti = (pty * WORLD_WIDTH + ptx);
	for (int cx = -1; cx <= 1; cx++)
	{
		for (int cy = -1; cy <= 1; cy++)
		{
			int ti = (pti + cx + cy * WORLD_WIDTH);
			if (ti < 0) continue;
			if (Map[ti] >= _TILE_FIRST_NOCOLLISION)
			{
				if (Map[ti] <= _TILE_LAST_DIGGED_OUT)
				{
					if (player_center.GetDistanceSq(ZL_Vector((ti % WORLD_WIDTH)+.5f, -(ti / WORLD_WIDTH)-.75f)) < (.4f*.4f))
					{
						Collect(player, Map[ti], ti);
						Map[ti] = TILE_EMPTY;
					}
				}
				continue;
			}
			ZL_Vector p((float)(ti % WORLD_WIDTH), -(ti / WORLD_WIDTH)-1.f);
			if (Map[ti+1          ] >= _TILE_FIRST_NOCOLLISION && player_center.x > p.x + 1) colsh.push_back(p+ZLV( 1,.5));
			if (Map[ti-1          ] >= _TILE_FIRST_NOCOLLISION && player_center.x < p.x    ) colsh.push_back(p+ZLV( 0,.5));
			if (Map[ti+WORLD_WIDTH] >= _TILE_FIRST_NOCOLLISION && player_center.y < p.y    ) colsv.push_back(p+ZLV(.5f,0));
			if (Map[ti-WORLD_WIDTH] >= _TILE_FIRST_NOCOLLISION && player_center.y > p.y + 1) colsv.push_back(p+ZLV(.5f,1));
		}
	}

	for (int i = (int)fallings.size(); i--;)
	{
		Falling& f = fallings[i];
		if (f.tile >= _TILE_FIRST_NOCOLLISION)
		{
			if (player_center.GetDistanceSq(ZL_Vector(f.x+.5f, f.y + .25f)) < (.4f*.4f))
			{
				Collect(player, f.tile, (int)sfloor(-f.y) * WORLD_WIDTH + (int)f.x);
				fallings.erase(fallings.begin() + i);
			}
			continue;
		}
		ZL_Vector p(f.x, f.y);
		if (player_center.x > p.x + 1) colsh.push_back(p+ZLV( 1,.5));
		if (player_center.x < p.x    ) colsh.push_back(p+ZLV( 0,.5));
		if (player_center.y < p.y    ) colsv.push_back(p+ZLV(.5f,0));
		if (player_center.y > p.y + 1) colsv.push_back(p+ZLV(.5f,1));
	}
	
	if (player.x > WORLD_WIDTH - 2 && player.oldy >= elevatorY - (PLAYER_HALFHEIGHT-.05f))
		colsv.push_back(ZLV(WORLD_WIDTH-.5f,elevatorY));

	player.oldy = player.y;
}

static void CheckCollision(Player& player)
{
	if (player.x < PLAYER_HALFWIDTH)
		player.x = PLAYER_HALFWIDTH;
	if (player.x > WORLD_WIDTH - PLAYER_HALFWIDTH)
		player.x = WORLD_WIDTH - PLAYER_HALFWIDTH;

	ZL_Rectf player_rec(player.x-PLAYER_HALFWIDTH, player.y, player.x+PLAYER_HALFWIDTH, player.y+2*PLAYER_HALFHEIGHT);
	
	for (ZL_Vector& p : colsh)
	{
		if (player_rec.right > p.x && player_rec.right - PLAYER_HALFWIDTH < p.x && player_rec.low+.01f < p.y+.5f && player_rec.high-.01f > p.y-.5f)
		{
			player.velx = 0;
			player.x = p.x - PLAYER_HALFWIDTH;
			player_rec = ZL_Rectf::FromCenter(player.x, player.y+PLAYER_HALFHEIGHT, PLAYER_HALFWIDTH, PLAYER_HALFHEIGHT);
		}
		if (player_rec.left < p.x && player_rec.left + PLAYER_HALFWIDTH > p.x && player_rec.low+.01f < p.y+.5f && player_rec.high-.01f > p.y-.5f)
		{
			player.velx = 0;
			player.x = p.x + PLAYER_HALFWIDTH;
			player_rec = ZL_Rectf::FromCenter(player.x, player.y+PLAYER_HALFHEIGHT, PLAYER_HALFWIDTH, PLAYER_HALFHEIGHT);
		}
	}

	for (ZL_Vector& p : colsv)
	{
		if (player_rec.low - .05f < p.y && player_rec.low + .25f > p.y && player_rec.left+.01f < p.x+.5f && player_rec.right-.01f > p.x-.5f)
		{
			if (player.vely <= 0)
			{
				player.vely = 0;
				player.jump = 0;
				player.grounded = true;
				player.groundedTicks = ZLTICKS;
			}
			player.y = p.y;
			player_rec = ZL_Rectf::FromCenter(player.x, player.y+PLAYER_HALFHEIGHT, PLAYER_HALFWIDTH, PLAYER_HALFHEIGHT);
		}
		if (player_rec.high-PLAYER_HALFHEIGHT < p.y && player_rec.high + .05f > p.y && player_rec.left+.01f < p.x+.5f && player_rec.right-.01f > p.x-.5f)
		{
			if (player.vely > 0) player.vely = 0;
			player.y = p.y - (PLAYER_HALFHEIGHT*2);
			player_rec = ZL_Rectf::FromCenter(player.x, player.y+PLAYER_HALFHEIGHT, PLAYER_HALFWIDTH, PLAYER_HALFHEIGHT);
			player.groundedTicks = 0;
		}
	}
}

static void Input()
{
	if (InShop || IsTitle) return;
	for (Player& player : players)
	{
		if (player.dead) continue;
		player.movex = player.AxisX();
		player.movey = player.AxisY();
		bool pressedAttack = player.ButtonAttack();
		bool pressedJump = player.ButtonJump();
		bool pressedDig = ZL_Input::Down(ZLK_K, true);

		if (pressedJump && (player.grounded || (player.vely <= 0 && ZLSINCE(player.groundedTicks) < 120)))
		{
			player.grounded = false;
			player.vely = 2.2f;
			player.jump++;
			sndJump.Play();
		}

		if (pressedDig)
		{
			int ptx = (int)sfloor(player.x), pty = (int)sfloor(-(player.y+PLAYER_HALFHEIGHT));
			int digti = (pty * WORLD_WIDTH + ptx) + WORLD_WIDTH;
			if (digti > 0) Map[digti] = TILE_EMPTY;
			digti = (pty * WORLD_WIDTH + ptx) + 1;
			if (digti > 0) Map[digti] = TILE_EMPTY;
			digti = (pty * WORLD_WIDTH + ptx) + 2;
			if (digti > 0) Map[digti] = TILE_EMPTY;
		}

		if (pressedAttack && player.attack == 0)
		{
			player.attack = 19;
		}
	}
}

static void Empty(int ti, bool destroyStuck)
{
	unsigned char spawn = 0;
	if (!destroyStuck)
	{
		switch (Map[ti])
		{
			case TILE_ROCK_STUCK:    case TILE_ROCK_FREE:    spawn = TILE_ROCK_FREE;    break;
			case TILE_BRONZE_STUCK:  case TILE_BRONZE_FREE:  spawn = TILE_BRONZE_FREE;  break;
			case TILE_SILVER_STUCK:  case TILE_SILVER_FREE:  spawn = TILE_SILVER_FREE;  break;
			case TILE_GOLD_STUCK:    case TILE_GOLD_FREE:    spawn = TILE_GOLD_FREE;    break;
			case TILE_DIAMOND_STUCK: case TILE_DIAMOND_FREE: spawn = TILE_DIAMOND_FREE; break;
			case TILE_GRAIL_STUCK:   case TILE_GRAIL_FREE:   spawn = TILE_GRAIL_FREE;   break;
		}
	}
	Map[ti] = TILE_EMPTY;
	if (spawn)
	{
		if (Map[ti+WORLD_WIDTH] < _TILE_FIRST_NOCOLLISION) Map[ti] = spawn;
		else fallings.push_back(Falling((float)(ti % WORLD_WIDTH), -(ti / WORLD_WIDTH)-1.f, spawn));
	}
	ti -= WORLD_WIDTH;
	if (ti < 0) return;

	if (Map[ti] == TILE_ROCK_STUCK || (Map[ti] >= _TILE_FIRST_DIGGED_OUT && Map[ti] <= _TILE_LAST_DIGGED_OUT))
	{
		Empty(ti);
	}
}

static void Update()
{
	if (InShop || IsTitle) return;
	bool elevatorUp = false, elevatorDown = false;
	float lowestPlayer = elevatorY;
	for (Player& player : players)
	{
		if (player.dead) continue;
		player.velx = player.movex;

		ZL_Vector movetotal;
		if (player.velx)
		{
			player.lookLeft = (player.velx < 0);
			movetotal.x = player.velx * TOMELAPSEDF(6);
		}
		if (!player.grounded)
		{
			player.vely -= TOMELAPSEDF(8);
			movetotal.y = player.vely * TOMELAPSEDF(4);
		}
		player.grounded = false;

		for (float step;;)
		{
			PrepareCollision(player);
			if (movetotal.x) { step = (movetotal.x < -.2f ? -.2f : movetotal.x > .2f ? .2f : movetotal.x); player.x += step; movetotal.x -= step; CheckCollision(player); }
			if (movetotal.y) { step = (movetotal.y < -.2f ? -.2f : movetotal.y > .2f ? .2f : movetotal.y); player.y += step; movetotal.y -= step; CheckCollision(player); }
			if (!movetotal) break;
		}

		CheckCollision(player);

		int ptx = (int)sfloor(player.x), pty = (int)sfloor(-(player.y+PLAYER_HALFHEIGHT)), pti = (pty * WORLD_WIDTH + ptx);
		if (pti >= WORLD_WIDTH && Map[pti] < _TILE_FIRST_NOCOLLISION)
		{
			player.dead = true;
			player.deaths++;
			sndDeath.Play();
			return;
		}

		if (player.attack)
		{
			player.attack--;
			if (player.attack == 16 && player.AxisY() < -.5f)
			{
				if (Tnt && pti >= WORLD_WIDTH && (ptx != WORLD_WIDTH-1 || !player.grounded))
				{
					bool exists = false;
					for (TNT& tnt : tnts) { if (tnt.ti == pti) { exists = true; break; } }
					if (!exists) { tnts.push_back(TNT(pti)); sndDrop.Play(); }
					Tnt--;
				}
				player.attack--; // skip real attack
			}
			else if (player.attack == 15)
			{
				bool digUp = (ptx != WORLD_WIDTH-1 && player.AxisY() > .5f);
				int digx = (int)sfloor(player.x + (digUp ? 0 : player.lookLeft ? -(PLAYER_HALFWIDTH+.3f) : (PLAYER_HALFWIDTH+.3f))), digy = pty + (digUp ? -1 : 0), digti = (digy * WORLD_WIDTH + digx);
				if (digti >= WORLD_WIDTH && digti/WORLD_WIDTH == digy)
				{
					if (Map[digti] >= _TILE_FIRST_DIGGABLE && Map[digti] <= _TILE_LAST_DIGGABLE)
					{
						Empty(digti);
						sndDig.Play();
					}
					if (!digUp && Map[digti] == TILE_ROCK_FREE)
					{
						int moveti = digti + (player.lookLeft ? -1 : 1);
						if (moveti >= WORLD_WIDTH && moveti/WORLD_WIDTH == digy && Map[moveti] >= _TILE_FIRST_NOCOLLISION && (moveti % WORLD_WIDTH) != WORLD_WIDTH-1)
						{
							Map[digti] = TILE_EMPTY;
							Map[moveti] = TILE_ROCK_FREE;
							Empty(digti);
							Empty(moveti);
							sndPush.Play();
						}
					}
				}
			}
		}

		if (player.x > WORLD_WIDTH - 2 && player.movey)
		{
			if (player.movey > 0) elevatorUp = true;
			if (player.movey < 0) elevatorDown = true;
		}

		if (player.y <= lowestPlayer) lowestPlayer = player.y - (player.x >= WORLD_WIDTH - 1 - PLAYER_HALFWIDTH*2 && player.y == elevatorY ? 10 : 0);
	}

	for (int i = 0; i != fallings.size(); i++)
	{
		Falling& f = fallings[i];
		f.vely -= TOMELAPSEDF(1.5);
		f.y += f.vely * TOMELAPSEDF(2);
		int fti = (int)sfloor(-f.y) * WORLD_WIDTH + (int)f.x;
		if (Map[fti] < _TILE_FIRST_NOCOLLISION)
		{
			while (Map[fti - WORLD_WIDTH] < _TILE_FIRST_NOCOLLISION && Map[fti - WORLD_WIDTH*2] == TILE_EMPTY) fti -= WORLD_WIDTH;
			if (Map[fti - WORLD_WIDTH] != TILE_GRAIL_FREE && Map[fti - WORLD_WIDTH] > f.tile) Map[fti - WORLD_WIDTH] = f.tile;
			(f.tile == TILE_ROCK_FREE ? sndBigDrop : sndDrop).Play();
			fallings.erase(fallings.begin() + i);
			i--;
		}
	}

	for (int i = 0; i != tnts.size(); i++)
	{
		TNT& tnt = tnts[i];
		tnt.timer++;
		if (tnt.timer == 60)
		{
			if ((tnt.ti % WORLD_WIDTH)) Empty(tnt.ti - 1, true);
			Empty(tnt.ti, true);
			if ((tnt.ti % WORLD_WIDTH) != WORLD_WIDTH-1) Empty(tnt.ti + 1, true);
			sndExplosion.Play();
		}
		if (tnt.timer == 80)
		{
			tnts.erase(tnts.begin() + i);
			i--;
		}
	}

	if (elevatorDown != elevatorUp || elevatorVel)
	{
		bool start = !elevatorVel;
		elevatorVel = ZL_Math::Clamp(elevatorVel + TOMELAPSEDF(6), 4.0f, 50.0f);

		float oldY = elevatorY;
		float elevatorMoveY = TOMELAPSEDF(elevatorVel) * ((elevatorDown ? -1 : 0) + (elevatorUp ? 1 : 0));
		elevatorMoveY = ZL_Math::Clamp(elevatorMoveY, ZL_Math::Max(lowestPlayer, (float)-ElevatorDepth) - elevatorY, -elevatorY);

		if (elevatorMoveY)
		{
			if (start) imcElevator.Stop().NoteOn(0, 60);
			elevatorY += elevatorMoveY;
			for (Player& player : players)
				if (player.x > WORLD_WIDTH - 1 - PLAYER_HALFWIDTH*2 && player.y == oldY)
					{ player.y = elevatorY; player.oldy += elevatorMoveY; if (player.y > -0.001f) player.vely = elevatorVel/5; }
		}
		else if (elevatorVel)
		{
			elevatorVel = 0;
			if (!start) imcElevator.Stop().NoteOn(2, 60);
		}

		if (elevatorY < oldY)
		{
			int elevatorTi = (int)-elevatorY * WORLD_WIDTH + WORLD_WIDTH - 1;
			if (Map[elevatorTi] != TILE_EMPTY) Empty(elevatorTi);
		}
	}
}

static void Draw()
{
	float screenRows = 32.f * (ZL_Display::Height / ZL_Display::Width);

	float minViewY = players[0].y + 5, maxViewY = players[0].y - 5;

	if (ScreenTop < minViewY) ScreenTop = minViewY;
	if (ScreenTop - screenRows > maxViewY) ScreenTop = maxViewY + screenRows;

	if (IsTitle)
	{
		ScreenTop = -10;
		ElevatorDepth = 30;
	}

	float screenBot = ScreenTop - screenRows;

	ZL_Display::PushOrtho(0, 32, screenBot, ScreenTop);

	ZL_Display::FillGradient(0, 0, 32, 16, ZLRGB(0,0,.3), ZLRGB(0,0,.3), ZLRGB(.4,.4,.6), ZLRGB(.4,.4,.6));

	int visRowTop = (int)MAX(-ScreenTop-1, 0);
	int visRowBot = (int)(-screenBot+1);
	AddMapRows(visRowBot);

	for (int y = visRowTop, ti = (y * WORLD_WIDTH); y <= visRowBot; y++)
	{
		for (int x = 0; x != 32; x++, ti++)
		{
			ZL_Vector p((float)x, -y-1.f);
			srfTiles.SetTilesetIndex(Map[ti] >= _TILE_FIRST_DIGGED_OUT ? GFX_TILE_EMPTY : GFX_TILE_DIRT).Draw(p);
			switch (Map[ti])
			{
				case TILE_STREET:        srfTiles.SetTilesetIndex(GFX_TILE_STREET   ).Draw(p); break;
				case TILE_ROCK_FREE:
				case TILE_ROCK_STUCK:    srfTiles.SetTilesetIndex(GFX_TILE_ROCK     ).Draw(p); break;
				case TILE_BRONZE_STUCK:  srfTiles.SetTilesetIndex(GFX_TILE_ORE_STUCK).Draw(p, ColorBronze ); break;
				case TILE_SILVER_STUCK:  srfTiles.SetTilesetIndex(GFX_TILE_ORE_STUCK).Draw(p, ColorSilver ); break;
				case TILE_GOLD_STUCK:    srfTiles.SetTilesetIndex(GFX_TILE_ORE_STUCK).Draw(p, ColorGold   ); break;
				case TILE_DIAMOND_STUCK: srfTiles.SetTilesetIndex(GFX_TILE_ORE_STUCK).Draw(p, ColorDiamond); break;
				case TILE_BRONZE_FREE:   srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE ).Draw(p, ColorBronze ); break;
				case TILE_SILVER_FREE:   srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE ).Draw(p, ColorSilver ); break;
				case TILE_GOLD_FREE:     srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE ).Draw(p, ColorGold   ); break;
				case TILE_DIAMOND_FREE:  srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE ).Draw(p, ColorDiamond); break;
				case TILE_GRAIL_STUCK:
				case TILE_GRAIL_FREE:    srfTiles.SetTilesetIndex(GFX_TILE_GRAIL    ).Draw(p);               break;
				default: continue;
			}
		}
		srfTiles.SetTilesetIndex(GFX_TILE_SHAFT).Draw(31, -y-1.f);
	}

	srfTiles.SetTilesetIndex(GFX_TILE_ELEVATOR).Draw(31, elevatorY);
	for (int i = 0; i != 3; i++)
	{
		if (i < 2) { srfTiles.SetTilesetIndex(GFX_HOME1+i).Draw(6.f+i, 1.f); srfTiles.SetTilesetIndex(GFX_HOME1+8+i).Draw(6.f+i, 0.f); }
		srfTiles.SetTilesetIndex(GFX_HOSPITAL1+i).Draw(14.f+i, 1.f); srfTiles.SetTilesetIndex(GFX_HOSPITAL1+8+i).Draw(14.f+i, 0.f);
		srfTiles.SetTilesetIndex(GFX_SHOP1+i).Draw(22.f+i, 1.f); srfTiles.SetTilesetIndex(GFX_SHOP1+8+i).Draw(22.f+i, 0.f);
	}
	if (WinTicks) srfTiles.SetTilesetIndex(GFX_TILE_GRAIL).Draw(6.5f, 1.9f);

	for (Falling& falling : fallings)
	{
		switch (falling.tile)
		{
			case TILE_ROCK_FREE:    srfTiles.SetTilesetIndex(GFX_TILE_ROCK    ).Draw(falling.x, falling.y); break;
			case TILE_BRONZE_FREE:  srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE).Draw(falling.x, falling.y, ColorBronze ); break;
			case TILE_SILVER_FREE:  srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE).Draw(falling.x, falling.y, ColorSilver ); break;
			case TILE_GOLD_FREE:    srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE).Draw(falling.x, falling.y, ColorGold   ); break;
			case TILE_DIAMOND_FREE: srfTiles.SetTilesetIndex(GFX_TILE_ORE_FREE).Draw(falling.x, falling.y, ColorDiamond); break;
			case TILE_GRAIL_FREE:   srfTiles.SetTilesetIndex(GFX_TILE_GRAIL   ).Draw(falling.x, falling.y              ); break;
			default: continue;
		}
		//ZL_Display::FillCircle(falling.x, falling.y, .1f, ZL_Color::Gray);
	}

	for (TNT& tnt : tnts)
	{
		if (tnt.timer >= 60)
		{
			for (int ti = tnt.ti - (tnt.ti % WORLD_WIDTH ? 1 : 0); ti <= tnt.ti + ((tnt.ti+1) % WORLD_WIDTH ? 1 : 0); ti++)
				srfTiles.SetTilesetIndex(GFX_EXPLOSION).Draw((float)(ti % WORLD_WIDTH), -(ti / WORLD_WIDTH)-1.f, RAND_ANGLE, RAND_RANGE(.4/32,1.2/32), RAND_RANGE(.4/32,1.2/32));
		}
		else srfTiles.SetTilesetIndex(GFX_ITEM_TNT).Draw((float)(tnt.ti % WORLD_WIDTH), -(tnt.ti / WORLD_WIDTH)-1.f);
	}

	for (Player& player : players)
	{
		if (player.dead)
		{
			if (player.PressAny())
			{
				Respawn(player);
			}
			else
			{
				fntBig.Draw(player.x, player.y+PLAYER_HALFHEIGHT, "YOU DIED (DIG) TO RESPAWN", .02f, .02f, ZL_Origin::Center);
				fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT, "YOU DIED (DIG) TO RESPAWN", .02f, .02f, ZLBLACK, ZL_Origin::Center);
				continue;
			}
		}
		ZL_Vector pos(player.x, player.y);
		bool atHome = false;
		if (!InShop && player.grounded && player.y >= -.25f && player.y < .25f)
		{
			atHome = (ZLV(7.f, 0.f).GetDistanceSq(pos) < 1);
			if (atHome && !WinTicks)
			{
				fntBig.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "PRESS (DIG) TO CHANGE OUTFIT", .015f, .015f, ZL_Origin::Center);
				fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "PRESS (DIG) TO CHANGE OUTFIT", .015f, .015f, ZLBLACK, ZL_Origin::Center);
				if (player.PressAny()) { player.Dress(); sndMenu.Play(); }
			}
			if (ZLV(HOSPITAL_DOOR_X, 0.f).GetDistanceSq(pos) < 1)
			{
				fntBig.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "YOU CAN RESPAWN HERE", .015f, .015f, ZL_Origin::Center);
				fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "YOU CAN RESPAWN HERE", .015f, .015f, ZLBLACK, ZL_Origin::Center);
			}
			if (ZLV(23.5f, 0.f).GetDistanceSq(pos) < 1)
			{
				fntBig.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "PRESS (DIG) TO ENTER SHOP", .015f, .015f, ZL_Origin::Center);
				fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "PRESS (DIG) TO ENTER SHOP", .015f, .015f, ZLBLACK, ZL_Origin::Center);
				if (player.PressAny()) { InShop = true; ShopCursor = 0; player.attack = 0; sndMenu.Play(); }
			}
			if (ZLV(WORLD_WIDTH, 0.f).GetDistanceSq(pos) < 1)
			{
				fntBig.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "PRESS (UP/DOWN) TO USE ELEVATOR", .015f, .015f, ZL_Origin::CenterRight);
				fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "PRESS (UP/DOWN) TO USE ELEVATOR", .015f, .015f, ZLBLACK, ZL_Origin::CenterRight);
			}
		}
		if (WinTicks && (ZLSINCE(WinTicks) < 5000 || atHome))
		{
			fntBig.Draw(       player.x, player.y+PLAYER_HALFHEIGHT*6, "YOU FOUND THE ANCIENT GRAIL!", .015f, .015f, ZL_Origin::Center);
			fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT*6, "YOU FOUND THE ANCIENT GRAIL!", .015f, .015f, ZLBLACK, ZL_Origin::Center);
			fntBig.Draw(       player.x, player.y+PLAYER_HALFHEIGHT*4, "YOU WIN! THANKS FOR PLAYING!", .015f, .015f, ZL_Origin::Center);
			fntBigOutline.Draw(player.x, player.y+PLAYER_HALFHEIGHT*4, "YOU WIN! THANKS FOR PLAYING!", .015f, .015f, ZLBLACK, ZL_Origin::Center);
		}
		srfPlayer.SetScale((player.lookLeft ? -1 : 1) / 32.f, 1 / 32.f);
		srfPlayer.SetTilesetIndex(GFX_PLAYER_FACE).Draw(pos, player.colorHead);
		srfPlayer.SetTilesetIndex(GFX_PLAYER_HAT).Draw(pos, player.colorHat);
		srfPlayer.SetTilesetIndex(player.grounded ? (player.velx ? GFX_PLAYER_LEG_WALK1 + (ZLTICKS/100)%3 : GFX_PLAYER_LEG_STAND) : GFX_PLAYER_LEG_JUMP).Draw(pos, player.colorLegs);
		srfPlayer.SetTilesetIndex(GFX_PLAYER_TORSO_HOLD + player.attack/5).Draw(pos, player.colorTorso);
		srfPlayer.SetTilesetIndex(GFX_PICKAXE_HOLD + player.attack/5).Draw(pos);
 		//ZL_Display::FillCircle(pos, .1f, player.color);
	}

	int darkRows = ZL_Math::Clamp(ElevatorDepth / 12, 1, 3);
	for (int i = 0; i != 10; i++)
	{
		float y = (float)(-ElevatorDepth+i*darkRows);
		ZL_Display::FillRect(0, y, WORLD_WIDTH, y+darkRows, ZLLUMA(0, (10-i)/11.0f));
	}
	ZL_Display::FillRect(0, -ElevatorDepth-999.f, WORLD_WIDTH, (float)-ElevatorDepth, ZL_Color::Black);


	#ifdef ZILLALOG
	//for (ZL_Vector& p : colsh) ZL_Display::FillRect(p.x-.05f,p.y-.5f,p.x+.05f,p.y+.5f, ZLRGBA(1,0,0,.5));
	//for (ZL_Vector& p : colsv) ZL_Display::FillRect(p.x-.5f,p.y-.05f,p.x+.5f,p.y+.05f, ZLRGBA(1,0,0,.5));
	//for (Player& player : players) ZL_Display::FillRect(ZL_Rectf(ZLV(player.x, player.y+PLAYER_HALFHEIGHT), ZLV(PLAYER_HALFWIDTH, PLAYER_HALFHEIGHT)), ZLRGBA(0,1,0,.5));

	ZL_Vector mp = ZL_Display::ScreenToWorld(ZL_Display::PointerX, ZL_Display::PointerY);
	int mpx = (int)sfloor(mp.x), mpy = (int)sfloor(mp.y);
	ZL_Display::FillRect((float)mpx, (float)mpy, mpx+1.f, mpy+1.f, ZLRGBA(0,0,1,.25));
	if (ZL_Input::Held(ZL_BUTTON_RIGHT) && mpy <= 0)
	{
		Empty(-(mpy+1) * WORLD_WIDTH + mpx);
	}
	if (ZL_Input::Down(ZL_BUTTON_MIDDLE))
	{
		players[0].x = mp.x;
		players[0].y = mp.y;
	}
	#endif

	ZL_Display::PopOrtho();

	if (IsTitle)
	{
		ZL_Vector rot = ZL_Vector::FromAngle(ZLTICKS/1000.f) * 20;
		fntTitle.Draw(       ZLHALFW+rot.x,         ZLHALFH + 220-rot.y,         "DIGZILLA", 2, 2, ZLLUMA(0,.5), ZL_Origin::Center);
		for (int i = 0; i != 9; i++)
			fntTitle.Draw(   ZLHALFW - 2 + 2*(i%3), ZLHALFH + 220 - 2 + 2*(i/3), "DIGZILLA", 2, 2, ZLBLACK, ZL_Origin::Center);
		fntTitle.Draw(       ZLHALFW,               ZLHALFH + 220,               "DIGZILLA", 2, 2, ColorGold, ZL_Origin::Center);

		fntBigOutline.Draw(ZLHALFW, ZLHALFH + 100, "Dig out valuable ores", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH + 100, "Dig out valuable ores", ZLLUM(.9), ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLHALFH +  60, "Extend the elevator depth at the shop", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH +  60, "Extend the elevator depth at the shop", ZLLUM(.9), ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLHALFH +  20, "Can you find the ancient grail?", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH +  20, "Can you find the ancient grail?", ZLLUM(.9), ZL_Origin::Center);

		fntBigOutline.Draw(ZLHALFW, ZLHALFH -  40, "CONTROLS:", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH -  40, "CONTROLS:", ZLLUM(1), ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLHALFH -  85, "ARROW KEYS: Move  /  SPACE/ALT: Jump  /  CTRL: Dig", ZLBLACK, ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH -  85, "ARROW KEYS: Move  /  SPACE/ALT: Jump  /  CTRL: Dig", ZLLUM(.6), ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLHALFH - 120, "UP+DIG: Dig Up  /  DOWN+DIG: Use TNT  / DIG ROCK: Move Rock", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH - 120, "UP+DIG: Dig Up  /  DOWN+DIG: Use TNT  / DIG ROCK: Move Rock", ZLLUM(.6), ZL_Origin::Center);

		fntBigOutline.Draw(ZLHALFW, ZLHALFH - 190, "TIPS:", ZLBLACK, ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH - 190, "TIPS:", ZLLUM(1), ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLHALFH - 235, "YOU CAN DIG UNDER A ROCK - IF YOU'RE FAST ENOUGH!", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH - 235, "YOU CAN DIG UNDER A ROCK - IF YOU'RE FAST ENOUGH!", ZLLUM(.6), ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLHALFH - 275, "TNT WON'T HURT YOU - IT ONLY DESTROYS ROCKS TO THE SIDE", ZLBLACK,   ZL_Origin::Center);
		fntBig       .Draw(ZLHALFW, ZLHALFH - 275, "TNT WON'T HURT YOU - IT ONLY DESTROYS ROCKS TO THE SIDE", ZLLUM(.6), ZL_Origin::Center);

		fntBigOutline.Draw(8, 12, "(C) 2021 - Bernhard Schelling", ZLBLACK,          ZL_Origin::BottomLeft);
		fntBig       .Draw(8, 12, "(C) 2021 - Bernhard Schelling", ZLRGBA(.5,1,1,.4), ZL_Origin::BottomLeft);

		if (players[0].PressAny())
		{
			sndRespawn.Play();
			Reset();
			IsTitle = false;
		}
		if (ZL_Input::Down(ZLK_ESCAPE))
		{
			ZL_Application::Quit();
		}
		return;
	}

	ZL_String strMoney = ZL_String::format("%d", Money);
	ZL_String strTNT = ZL_String::format("%d", Tnt);
	ZL_String strDepth = ZL_String::format("Depth: %d m", ZL_Math::Max(0, (int)-players[0].y));
	ZL_String strEleva = ZL_String::format("Elevator Depth: %d m", ElevatorDepth);

	srfTiles.SetTilesetIndex(GFX_HUD_MONEY).DrawTo(ZL_Rectf::BySize(9, ZLFROMH(40+8), 40, 40));
	fntBigOutline.Draw(60-1, ZLFROMH(40), strMoney, ZLBLACK);
	fntBig.Draw(60, ZLFROMH(40), strMoney);

	srfTiles.SetTilesetIndex(GFX_HUD_TNT).DrawTo(ZL_Rectf::BySize(9, ZLFROMH(80+8), 40, 40));
	fntBigOutline.Draw(60-1, ZLFROMH(80), strTNT, ZLBLACK);
	fntBig.Draw(60, ZLFROMH(80), strTNT);

	fntBigOutline.Draw(ZLHALFW, ZLFROMH(40), strDepth, ZLBLACK, ZL_Origin::TopCenter);
	fntBig.Draw(ZLHALFW, ZLFROMH(40), strDepth, ZL_Origin::TopCenter);

	fntBigOutline.Draw(ZLFROMW(11), ZLFROMH(40), strEleva, ZLBLACK, ZL_Origin::TopRight);
	fntBig.Draw(ZLFROMW(10), ZLFROMH(40), strEleva, ZL_Origin::TopRight);

	if (InShop)
	{
		ZL_Display::FillRect(ZLHALFW-450, ZLFROMH(500), ZLHALFW+450, ZLFROMH(50), ZLLUMA(.2, .7));
		fntBig.Draw(ZLHALFW, ZLFROMH(100), "SHOP", ZL_Color::Yellow, ZL_Origin::Center);
		fntBigOutline.Draw(ZLHALFW, ZLFROMH(100), "SHOP", ZLBLACK, ZL_Origin::Center);

		ShopCursor = ZL_Math::Clamp(ShopCursor - players[0].PressUpDown(), 0, 2);
		ZL_Display::DrawRect(ZLHALFW-400, ZLFROMH(165+(ShopCursor*100)), ZLHALFW+400, ZLFROMH(230+(ShopCursor*100)), ZLBLACK, ZLLUMA(.1, .5));
		fntBig.Draw(ZLHALFW, ZLFROMH(200), ZL_String::format("BUY UP TO %d m ELEVATOR DEPTH (%d $)", ElevatorDepth + 10, ElevatorDepth * 10), (Money >= ElevatorDepth * 10 ? ZLWHITE : ZL_Color::Gray), ZL_Origin::Center);
		fntBig.Draw(ZLHALFW, ZLFROMH(300), "BUY TNT (500 $)", (Money >= 500 ? ZLWHITE : ZL_Color::Gray), ZL_Origin::Center);
		fntBig.Draw(ZLHALFW, ZLFROMH(400), "LEAVE", ZL_Origin::Center);

		if (players[0].PressAny())
		{
			switch (ShopCursor)
			{
				case 0:
					if (Money >= ElevatorDepth * 10)
					{
						sndMenu.Play();
						Money -= ElevatorDepth * 10;
						ElevatorDepth += 10;
					}
					else imcElevator.Stop().NoteOn(2, 60);
					break;
				case 1:
					if (Money >= 500)
					{
						sndMenu.Play();
						Money -= 500;
						Tnt++;
					}
					else imcElevator.Stop().NoteOn(2, 60);
					break;
				case 2:
					InShop = false;
					sndMenu.Play();
					break;
			}
		}
	}

	if (!players[0].dead && ZL_Input::Down(ZLK_ESCAPE))
	{
		if (players[0].y < - .5f) //(players[0].x != HOSPITAL_DOOR_X || players[0].y != 0)
		{
			players[0].dead = true;
			players[0].deaths++;
			sndDeath.Play();
		}
		else
		{
			Reset();
			IsTitle = true;
		}
	}
}

static struct sDigzilla : public ZL_Application
{
	sDigzilla() : ZL_Application(60) { }

	virtual void Load(int argc, char *argv[])
	{
		if (!ZL_Application::LoadReleaseDesktopDataBundle()) return;
		if (!ZL_Display::Init("Digzilla", 1280, 720, ZL_DISPLAY_ALLOWRESIZEHORIZONTAL)) return;
		ZL_Display::ClearFill(ZL_Color::White);
		ZL_Display::SetAA(true);
		ZL_Audio::Init();
		ZL_Input::Init();
		ZL_Application::SettingsInit("Digzilla");

		fntTitle = ZL_Font("Data/cowboys.ttf.zip", 80, true);
		fntBig = ZL_Font("Data/cowboys.ttf.zip", 28, true);
		fntBigOutline = ZL_Font("Data/cowboys.ttf.zip", 28, true, 3,3,3,3);
		srfTiles = ZL_Surface("Data/gfx.png").SetTilesetClipping(8, 8).SetScale(1 / 32.f);
		srfPlayer = srfTiles.Clone().SetOrigin(ZL_Origin::BottomCenter);

		extern TImcSongData imcDataIMCJUMP, imcDataIMCCOLLECT, imcDataIMCDIG, imcDataIMCPUSH, imcDataIMCBIGDROP, imcDataIMCDROP, imcDataIMCELEVATOR, imcDataIMCDEATH, imcDataIMCRESPAWN, imcDataIMCWIN, imcDataIMCMENU, imcDataIMCEXPLOSION, imcDataIMCMUSIC;
		sndJump = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCJUMP);
		sndCollect = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCCOLLECT);
		sndDig = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCDIG);
		sndPush = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCPUSH);
		sndBigDrop = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCBIGDROP);
		sndDrop = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCDROP);
		imcElevator = ZL_SynthImcTrack(&imcDataIMCELEVATOR);
		sndDeath = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCDEATH);
		sndRespawn = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCRESPAWN);
		imcWin = ZL_SynthImcTrack(&imcDataIMCWIN, false);
		sndMenu = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCMENU);
		sndExplosion = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCEXPLOSION);
		imcMusic = ZL_SynthImcTrack(&imcDataIMCMUSIC);
		imcMusic.Play();
		

		Reset();
	}

	virtual void AfterFrame()
	{
		Input();
		#ifdef ZILLALOG
		#define STEPTPF (ZL_Display::KeyDown[ZLK_LSHIFT] ? .1f : TOMTPF)
		#else
		#define STEPTPF TOMTPF
		#endif
		static float accumulate = 0;
		for (accumulate += ZLELAPSED; accumulate > STEPTPF; accumulate -= STEPTPF)
			Update();
		Draw();
	}
} Digzilla;

#if 1 // sound and music
static const unsigned int IMCJUMP_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCJUMP_PatternData[] = {
	0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCJUMP_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCJUMP_EnvList[] = {
	{ 0, 256, 69, 8, 16, 255, true, 255, },
	{ 50, 256, 137, 26, 13, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCJUMP_EnvCounterList[] = {
	{ 0, 0, 256 }, { -1, -1, 256 }, { 1, 0, 56 },
};
static const TImcSongOscillator IMCJUMP_OscillatorList[] = {
	{ 7, 48, IMCSONGOSCTYPE_SINE, 0, -1, 100, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static unsigned char IMCJUMP_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCJUMP_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCJUMP_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCJUMP = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 8, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 100,
	IMCJUMP_OrderTable, IMCJUMP_PatternData, IMCJUMP_PatternLookupTable, IMCJUMP_EnvList, IMCJUMP_EnvCounterList, IMCJUMP_OscillatorList, NULL,
	IMCJUMP_ChannelVol, IMCJUMP_ChannelEnvCounter, IMCJUMP_ChannelStopNote };

static const unsigned int IMCCOLLECT_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCCOLLECT_PatternData[] = {
	0x50, 0x55, 0x57, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCCOLLECT_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCCOLLECT_EnvList[] = {
	{ 0, 256, 79, 5, 18, 255, false, 0, },
	{ 0, 256, 174, 24, 13, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCCOLLECT_EnvCounterList[] = {
	{ 0, 0, 238 }, { -1, -1, 256 }, { 1, 0, 0 },
};
static const TImcSongOscillator IMCCOLLECT_OscillatorList[] = {
	{ 8, 200, IMCSONGOSCTYPE_SINE, 0, -1, 208, 1, 1 },
	{ 9, 31, IMCSONGOSCTYPE_SINE, 0, -1, 212, 2, 1 },
	{ 10, 106, IMCSONGOSCTYPE_SINE, 0, 0, 222, 1, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCCOLLECT_EffectList[] = {
	{ 100, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
};
static unsigned char IMCCOLLECT_ChannelVol[8] = { 69, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCCOLLECT_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCCOLLECT_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCCOLLECT = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2979, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 10, /*EFFECTLISTSIZE*/ 1, /*VOL*/ 100,
	IMCCOLLECT_OrderTable, IMCCOLLECT_PatternData, IMCCOLLECT_PatternLookupTable, IMCCOLLECT_EnvList, IMCCOLLECT_EnvCounterList, IMCCOLLECT_OscillatorList, IMCCOLLECT_EffectList,
	IMCCOLLECT_ChannelVol, IMCCOLLECT_ChannelEnvCounter, IMCCOLLECT_ChannelStopNote };


static const unsigned int IMCDIG_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCDIG_PatternData[] = {
	0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCDIG_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCDIG_EnvList[] = {
	{ 0, 256, 244, 0, 24, 255, true, 255, },
	{ 0, 256, 244, 0, 255, 255, true, 255, },
	{ 100, 200, 30, 5, 255, 255, true, 255, },
	{ 0, 256, 38, 0, 24, 255, true, 255, },
	{ 0, 256, 25, 2, 255, 255, true, 255, },
	{ 0, 256, 38, 11, 255, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCDIG_EnvCounterList[] = {
	{ 0, 0, 128 }, { 1, 0, 128 }, { -1, -1, 72 }, { 2, 0, 192 },
	{ -1, -1, 256 }, { 3, 0, 128 }, { 4, 0, 184 }, { 5, 0, 238 },
};
static const TImcSongOscillator IMCDIG_OscillatorList[] = {
	{ 7, 221, IMCSONGOSCTYPE_SINE, 0, -1, 132, 1, 2 },
	{ 8, 200, IMCSONGOSCTYPE_SINE, 0, -1, 68, 5, 4 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 150, 3, 4 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 1, 254, 4, 4 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCDIG_EffectList[] = {
	{ 86, 197, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 6, 7 },
	{ 99, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 4, 0 },
};
static unsigned char IMCDIG_ChannelVol[8] = { 143, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCDIG_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCDIG_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCDIG = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 3307, /*ENVLISTSIZE*/ 6, /*ENVCOUNTERLISTSIZE*/ 8, /*OSCLISTSIZE*/ 11, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 197,
	IMCDIG_OrderTable, IMCDIG_PatternData, IMCDIG_PatternLookupTable, IMCDIG_EnvList, IMCDIG_EnvCounterList, IMCDIG_OscillatorList, IMCDIG_EffectList,
	IMCDIG_ChannelVol, IMCDIG_ChannelEnvCounter, IMCDIG_ChannelStopNote };

static const unsigned int IMCPUSH_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCPUSH_PatternData[] = {
	0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCPUSH_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCPUSH_EnvList[] = {
	{ 0, 256, 145, 0, 24, 255, true, 255, },
	{ 0, 256, 145, 27, 255, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCPUSH_EnvCounterList[] = {
	{ 0, 0, 128 }, { 1, 0, 18 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCPUSH_OscillatorList[] = {
	{ 6, 174, IMCSONGOSCTYPE_SINE, 0, -1, 132, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 48, 2, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCPUSH_EffectList[] = {
	{ 66, 80, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 2, 2 },
};
static unsigned char IMCPUSH_ChannelVol[8] = { 143, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCPUSH_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCPUSH_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCPUSH = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 1, /*VOL*/ 197,
	IMCPUSH_OrderTable, IMCPUSH_PatternData, IMCPUSH_PatternLookupTable, IMCPUSH_EnvList, IMCPUSH_EnvCounterList, IMCPUSH_OscillatorList, IMCPUSH_EffectList,
	IMCPUSH_ChannelVol, IMCPUSH_ChannelEnvCounter, IMCPUSH_ChannelStopNote };

static const unsigned int IMCBIGDROP_OrderTable[] = {
	0x011000001,
};
static const unsigned char IMCBIGDROP_PatternData[] = {
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCBIGDROP_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 2, };
static const TImcSongEnvelope IMCBIGDROP_EnvList[] = {
	{ 0, 256, 277, 8, 16, 4, true, 255, },
	{ 0, 256, 209, 8, 16, 255, true, 255, },
	{ 64, 256, 261, 8, 15, 255, true, 255, },
	{ 0, 256, 3226, 8, 16, 255, true, 255, },
	{ 0, 386, 45, 8, 16, 255, true, 255, },
	{ 0, 256, 33, 8, 16, 255, true, 255, },
	{ 128, 256, 99, 8, 16, 255, true, 255, },
	{ 0, 128, 50, 8, 16, 255, true, 255, },
	{ 0, 256, 201, 5, 19, 255, true, 255, },
	{ 0, 256, 133, 8, 16, 255, true, 255, },
	{ 0, 256, 92, 8, 16, 255, true, 255, },
	{ 0, 256, 228, 8, 16, 255, true, 255, },
	{ 0, 256, 444, 8, 16, 255, true, 255, },
	{ 0, 256, 627, 23, 15, 255, true, 255, },
	{ 0, 256, 174, 8, 16, 255, true, 255, },
	{ 256, 271, 65, 8, 16, 255, true, 255, },
	{ 0, 512, 11073, 0, 255, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCBIGDROP_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 256 }, { -1, -1, 256 },
	{ 3, 0, 256 }, { 4, 6, 386 }, { 5, 6, 256 }, { 6, 6, 256 },
	{ 7, 6, 128 }, { -1, -1, 258 }, { 8, 6, 238 }, { 9, 6, 256 },
	{ 10, 7, 256 }, { 11, 7, 256 }, { 12, 7, 256 }, { -1, -1, 384 },
	{ 13, 7, 0 }, { 14, 7, 256 }, { 15, 7, 271 }, { 16, 7, 256 },
};
static const TImcSongOscillator IMCBIGDROP_OscillatorList[] = {
	{ 6, 127, IMCSONGOSCTYPE_SINE, 0, -1, 206, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, -1, 186, 4, 3 },
	{ 7, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 152, 3, 3 },
	{ 4, 227, IMCSONGOSCTYPE_SINE, 6, -1, 255, 6, 7 },
	{ 9, 15, IMCSONGOSCTYPE_NOISE, 6, -1, 255, 8, 9 },
	{ 4, 150, IMCSONGOSCTYPE_SINE, 6, -1, 255, 10, 3 },
	{ 5, 174, IMCSONGOSCTYPE_SINE, 6, -1, 230, 11, 3 },
	{ 6, 238, IMCSONGOSCTYPE_SINE, 7, -1, 0, 13, 3 },
	{ 5, 66, IMCSONGOSCTYPE_SINE, 7, -1, 134, 14, 15 },
	{ 7, 127, IMCSONGOSCTYPE_NOISE, 7, -1, 0, 16, 3 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 7, -1, 0, 3, 3 },
	{ 6, 106, IMCSONGOSCTYPE_SINE, 7, -1, 142, 17, 18 },
	{ 5, 200, IMCSONGOSCTYPE_SAW, 7, -1, 104, 3, 3 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 7, 8, 212, 3, 3 },
	{ 7, 0, IMCSONGOSCTYPE_NOISE, 7, 12, 228, 3, 19 },
};
static const TImcSongEffect IMCBIGDROP_EffectList[] = {
	{ 9906, 843, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 3 },
	{ 142, 58, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 3, 3 },
	{ 209, 0, 1, 6, IMCSONGEFFECTTYPE_LOWPASS, 3, 0 },
	{ 165, 208, 1, 6, IMCSONGEFFECTTYPE_RESONANCE, 3, 3 },
	{ 13716, 267, 1, 6, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 3 },
	{ 162, 206, 1, 7, IMCSONGEFFECTTYPE_RESONANCE, 3, 3 },
	{ 161, 0, 1, 7, IMCSONGEFFECTTYPE_LOWPASS, 3, 0 },
};
static unsigned char IMCBIGDROP_ChannelVol[8] = { 71, 0, 100, 100, 100, 100, 12, 104 };
static const unsigned char IMCBIGDROP_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 5, 12 };
static const bool IMCBIGDROP_ChannelStopNote[8] = { false, false, false, false, false, false, true, true };
TImcSongData imcDataIMCBIGDROP = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 17, /*ENVCOUNTERLISTSIZE*/ 20, /*OSCLISTSIZE*/ 15, /*EFFECTLISTSIZE*/ 7, /*VOL*/ 100,
	IMCBIGDROP_OrderTable, IMCBIGDROP_PatternData, IMCBIGDROP_PatternLookupTable, IMCBIGDROP_EnvList, IMCBIGDROP_EnvCounterList, IMCBIGDROP_OscillatorList, IMCBIGDROP_EffectList,
	IMCBIGDROP_ChannelVol, IMCBIGDROP_ChannelEnvCounter, IMCBIGDROP_ChannelStopNote };

static const unsigned int IMCDROP_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCDROP_PatternData[] = {
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCDROP_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCDROP_EnvList[] = {
	{ 0, 256, 277, 8, 16, 4, true, 255, },
	{ 0, 256, 209, 8, 16, 255, true, 255, },
	{ 64, 256, 261, 8, 15, 255, true, 255, },
	{ 0, 256, 3226, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCDROP_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 256 }, { -1, -1, 256 },
	{ 3, 0, 256 },
};
static const TImcSongOscillator IMCDROP_OscillatorList[] = {
	{ 6, 200, IMCSONGOSCTYPE_SINE, 0, -1, 140, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, -1, 128, 4, 3 },
	{ 7, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 28, 3, 3 },
};
static const TImcSongEffect IMCDROP_EffectList[] = {
	{ 9906, 843, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 3 },
	{ 142, 58, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 3, 3 },
};
static unsigned char IMCDROP_ChannelVol[8] = { 71, 0, 100, 100, 100, 100, 12, 104 };
static const unsigned char IMCDROP_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCDROP_ChannelStopNote[8] = { false, false, false, false, false, false, true, true };
TImcSongData imcDataIMCDROP = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 4, /*ENVCOUNTERLISTSIZE*/ 5, /*OSCLISTSIZE*/ 3, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 100,
	IMCDROP_OrderTable, IMCDROP_PatternData, IMCDROP_PatternLookupTable, IMCDROP_EnvList, IMCDROP_EnvCounterList, IMCDROP_OscillatorList, IMCDROP_EffectList,
	IMCDROP_ChannelVol, IMCDROP_ChannelEnvCounter, IMCDROP_ChannelStopNote };

static const unsigned int IMCELEVATOR_OrderTable[] = {
	0x000000000,
};
static const unsigned char IMCELEVATOR_PatternData[] = {
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCELEVATOR_PatternLookupTable[] = { 0, 1, 1, 2, 2, 2, 2, 2, };
static const TImcSongEnvelope IMCELEVATOR_EnvList[] = {
	{ 0, 256, 107, 27, 13, 255, true, 255, },
	{ 0, 256, 435, 24, 255, 255, true, 255, },
	{ 20, 128, 72, 28, 12, 255, true, 255, },
	{ 0, 256, 871, 25, 255, 255, true, 255, },
	{ 20, 148, 72, 27, 13, 255, true, 255, },
	{ 0, 256, 871, 8, 255, 255, true, 255, },
	{ 0, 256, 35, 10, 14, 255, true, 255, },
	{ 64, 128, 35, 8, 17, 255, true, 255, },
	{ 0, 148, 35, 8, 11, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCELEVATOR_EnvCounterList[] = {
	{ 0, 0, 18 }, { 1, 0, 0 }, { 2, 0, 33 }, { 3, 0, 2 },
	{ 4, 0, 29 }, { 5, 0, 256 }, { 6, 2, 248 }, { 1, 2, 0 },
	{ 7, 2, 128 }, { 3, 2, 2 }, { 8, 2, 148 }, { 5, 2, 256 },
};
static const TImcSongOscillator IMCELEVATOR_OscillatorList[] = {
	{ 7, 31, IMCSONGOSCTYPE_SINE, 0, -1, 64, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 28, 3, 4 },
	{ 7, 31, IMCSONGOSCTYPE_SINE, 2, -1, 64, 7, 8 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 28, 9, 10 },
};
static const TImcSongEffect IMCELEVATOR_EffectList[] = {
	{ 1524, 881, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 5 },
	{ 1524, 881, 1, 2, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 11 },
};
static unsigned char IMCELEVATOR_ChannelVol[8] = { 128, 128, 128, 100, 100, 100, 100, 100 };
static const unsigned char IMCELEVATOR_ChannelEnvCounter[8] = { 0, 0, 6, 0, 0, 0, 0, 0 };
static const bool IMCELEVATOR_ChannelStopNote[8] = { true, true, true, false, false, false, false, false };
TImcSongData imcDataIMCELEVATOR = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 6615, /*ENVLISTSIZE*/ 9, /*ENVCOUNTERLISTSIZE*/ 12, /*OSCLISTSIZE*/ 4, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 100,
	IMCELEVATOR_OrderTable, IMCELEVATOR_PatternData, IMCELEVATOR_PatternLookupTable, IMCELEVATOR_EnvList, IMCELEVATOR_EnvCounterList, IMCELEVATOR_OscillatorList, IMCELEVATOR_EffectList,
	IMCELEVATOR_ChannelVol, IMCELEVATOR_ChannelEnvCounter, IMCELEVATOR_ChannelStopNote };

static const unsigned int IMCDEATH_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCDEATH_PatternData[] = {
	0x24, 0, 0, 0, 0x20, 0, 0x22, 0, 0, 0, 0x1B, 0, 0x20, 0, 0, 0,
};
static const unsigned char IMCDEATH_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCDEATH_EnvList[] = {
	{ 0, 256, 65, 8, 16, 255, true, 255, },
	{ 0, 256, 26, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCDEATH_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCDEATH_OscillatorList[] = {
	{ 9, 66, IMCSONGOSCTYPE_SAW, 0, -1, 100, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, 0, 4, 2, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCDEATH_EffectList[] = {
	{ 85, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 2, 0 },
};
static unsigned char IMCDEATH_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCDEATH_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCDEATH_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCDEATH = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 3307, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 1, /*VOL*/ 100,
	IMCDEATH_OrderTable, IMCDEATH_PatternData, IMCDEATH_PatternLookupTable, IMCDEATH_EnvList, IMCDEATH_EnvCounterList, IMCDEATH_OscillatorList, IMCDEATH_EffectList,
	IMCDEATH_ChannelVol, IMCDEATH_ChannelEnvCounter, IMCDEATH_ChannelStopNote };

static const unsigned int IMCRESPAWN_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCRESPAWN_PatternData[] = {
	0x50, 0x52, 0x54, 0x55, 0x52, 0x54, 0x55, 0x57, 0x54, 0x55, 0x57, 0x59, 0, 0, 0, 0,
};
static const unsigned char IMCRESPAWN_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCRESPAWN_EnvList[] = {
	{ 0, 256, 370, 8, 16, 255, true, 255, },
	{ 0, 256, 741, 25, 15, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCRESPAWN_EnvCounterList[] = {
	{ 0, 0, 256 }, { -1, -1, 256 }, { 1, 0, 2 },
};
static const TImcSongOscillator IMCRESPAWN_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 174, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCRESPAWN_EffectList[] = {
	{ 163, 0, 2594, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 97, 0, 1, 0, IMCSONGEFFECTTYPE_HIGHPASS, 1, 0 },
};
static unsigned char IMCRESPAWN_ChannelVol[8] = { 128, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCRESPAWN_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCRESPAWN_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCRESPAWN = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 8, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 100,
	IMCRESPAWN_OrderTable, IMCRESPAWN_PatternData, IMCRESPAWN_PatternLookupTable, IMCRESPAWN_EnvList, IMCRESPAWN_EnvCounterList, IMCRESPAWN_OscillatorList, IMCRESPAWN_EffectList,
	IMCRESPAWN_ChannelVol, IMCRESPAWN_ChannelEnvCounter, IMCRESPAWN_ChannelStopNote };

static const unsigned int IMCWIN_OrderTable[] = {
	0x000000001, 0x000000002,
};
static const unsigned char IMCWIN_PatternData[] = {
	0x40, 0x40, 0x40, 0, 0x50, 0, 0, 0, 0x50, 0x50, 0x50, 0, 0x60, 0, 0, 0,
	0x60, 0, 0x57, 0, 0x55, 0, 0x5B, 0, 0x5B, 0, 0x5B, 0, 0x62, 0, 0, 0,
};
static const unsigned char IMCWIN_PatternLookupTable[] = { 0, 2, 2, 2, 2, 2, 2, 2, };
static const TImcSongEnvelope IMCWIN_EnvList[] = {
	{ 0, 256, 64, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCWIN_EnvCounterList[] = {
	{ 0, 0, 256 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCWIN_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 255, 1, 1 },
	{ 10, 0, IMCSONGOSCTYPE_SINE, 0, 0, 255, 1, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCWIN_EffectList[] = {
	{ 117, 0, 10176, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 255, 0, 1, 0, IMCSONGEFFECTTYPE_HIGHPASS, 1, 0 },
};
static unsigned char IMCWIN_ChannelVol[8] = { 194, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCWIN_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCWIN_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCWIN = {
	/*LEN*/ 0x2, /*ROWLENSAMPLES*/ 5088, /*ENVLISTSIZE*/ 1, /*ENVCOUNTERLISTSIZE*/ 2, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 100,
	IMCWIN_OrderTable, IMCWIN_PatternData, IMCWIN_PatternLookupTable, IMCWIN_EnvList, IMCWIN_EnvCounterList, IMCWIN_OscillatorList, IMCWIN_EffectList,
	IMCWIN_ChannelVol, IMCWIN_ChannelEnvCounter, IMCWIN_ChannelStopNote };

static const unsigned int IMCMENU_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCMENU_PatternData[] = {
	0x50, 0x54, 0x59, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCMENU_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCMENU_EnvList[] = {
	{ 0, 150, 144, 1, 23, 255, true, 255, },
	{ 0, 256, 699, 8, 16, 255, true, 255, },
	{ 0, 256, 172, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCMENU_EnvCounterList[] = {
	{ 0, 0, 92 }, { 1, 0, 256 }, { -1, -1, 256 }, { 2, 0, 256 },
};
static const TImcSongOscillator IMCMENU_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, -1, 230, 1, 2 },
	{ 8, 200, IMCSONGOSCTYPE_SINE, 0, -1, 124, 3, 2 },
	{ 10, 66, IMCSONGOSCTYPE_SQUARE, 0, -1, 82, 2, 2 },
};
static unsigned char IMCMENU_ChannelVol[8] = { 84, 84, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCMENU_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCMENU_ChannelStopNote[8] = { true, true, false, false, false, false, false, false };
TImcSongData imcDataIMCMENU = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 3307, /*ENVLISTSIZE*/ 3, /*ENVCOUNTERLISTSIZE*/ 4, /*OSCLISTSIZE*/ 3, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 63,
	IMCMENU_OrderTable, IMCMENU_PatternData, IMCMENU_PatternLookupTable, IMCMENU_EnvList, IMCMENU_EnvCounterList, IMCMENU_OscillatorList, NULL,
	IMCMENU_ChannelVol, IMCMENU_ChannelEnvCounter, IMCMENU_ChannelStopNote };

static const unsigned int IMCEXPLOSION_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCEXPLOSION_PatternData[] = {
	0x44, 255, 0x47, 255, 0x55, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCEXPLOSION_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCEXPLOSION_EnvList[] = {
	{ 0, 256, 78, 8, 16, 0, true, 255, },
	{ 0, 256, 5, 0, 24, 255, true, 255, },
	{ 0, 256, 42, 8, 16, 255, true, 255, },
	{ 0, 256, 87, 5, 19, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCEXPLOSION_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 128 }, { 2, 0, 256 }, { 3, 0, 238 },
	{ -1, -1, 256 },
};
static const TImcSongOscillator IMCEXPLOSION_OscillatorList[] = {
	{ 6, 169, IMCSONGOSCTYPE_SQUARE, 0, -1, 80, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 90, 3, 4 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCEXPLOSION_EffectList[] = {
	{ 17272, 373, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 4 },
};
static unsigned char IMCEXPLOSION_ChannelVol[8] = { 99, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCEXPLOSION_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCEXPLOSION_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCEXPLOSION = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 4, /*ENVCOUNTERLISTSIZE*/ 5, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 1, /*VOL*/ 179,
	IMCEXPLOSION_OrderTable, IMCEXPLOSION_PatternData, IMCEXPLOSION_PatternLookupTable, IMCEXPLOSION_EnvList, IMCEXPLOSION_EnvCounterList, IMCEXPLOSION_OscillatorList, IMCEXPLOSION_EffectList,
	IMCEXPLOSION_ChannelVol, IMCEXPLOSION_ChannelEnvCounter, IMCEXPLOSION_ChannelStopNote };

static const unsigned int IMCMUSIC_OrderTable[] = {
	0x000000001, 0x000000001, 0x000000002, 0x000000002, 0x000000111, 0x000000122, 0x000000131, 0x000000142,
	0x000000141, 0x000000130,
};
static const unsigned char IMCMUSIC_PatternData[] = {
	0x40, 0, 0x40, 0, 0x44, 0, 0x44, 0, 0x45, 0, 0x40, 0, 0x44, 0, 0x42, 0,
	0x49, 0, 0x49, 0, 0x42, 0, 0x47, 0, 0x45, 0, 0x4B, 0, 0x50, 0, 0x52, 0,
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x40, 0x42, 0x44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x52, 0, 0, 0, 0, 0, 0, 0, 0x62, 0, 0, 0, 0, 0, 0, 0,
	0x50, 0, 0, 0, 0x50, 0, 0, 0, 0x50, 0, 0, 0, 0x50, 0, 0, 0,
};
static const unsigned char IMCMUSIC_PatternLookupTable[] = { 0, 2, 6, 7, 7, 7, 7, 7, };
static const TImcSongEnvelope IMCMUSIC_EnvList[] = {
	{ 0, 256, 87, 8, 16, 255, true, 255, },
	{ 0, 256, 5, 8, 16, 255, false, 255, },
	{ 0, 256, 64, 8, 16, 255, true, 255, },
	{ 0, 386, 65, 8, 16, 255, true, 255, },
	{ 0, 256, 174, 8, 16, 255, true, 255, },
	{ 128, 256, 130, 8, 16, 255, true, 255, },
	{ 0, 128, 1046, 8, 16, 255, true, 255, },
	{ 0, 256, 348, 5, 19, 255, true, 255, },
	{ 0, 256, 418, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCMUSIC_EnvCounterList[] = {
	{ 0, 0, 256 }, { -1, -1, 256 }, { 1, 0, 256 }, { 2, 1, 256 },
	{ 3, 2, 386 }, { 4, 2, 256 }, { 5, 2, 256 }, { 6, 2, 128 },
	{ 7, 2, 238 }, { 8, 2, 256 },
};
static const TImcSongOscillator IMCMUSIC_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SAW, 0, -1, 100, 1, 1 },
	{ 9, 0, IMCSONGOSCTYPE_SINE, 0, 0, 100, 1, 1 },
	{ 9, 0, IMCSONGOSCTYPE_SINE, 1, -1, 144, 1, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, 2, 160, 1, 1 },
	{ 5, 150, IMCSONGOSCTYPE_SINE, 2, -1, 255, 5, 6 },
	{ 9, 0, IMCSONGOSCTYPE_NOISE, 2, -1, 255, 7, 1 },
	{ 5, 200, IMCSONGOSCTYPE_SINE, 2, -1, 160, 8, 1 },
	{ 5, 31, IMCSONGOSCTYPE_SINE, 2, -1, 255, 9, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCMUSIC_EffectList[] = {
	{ 117, 0, 16536, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 255, 0, 1, 0, IMCSONGEFFECTTYPE_HIGHPASS, 2, 0 },
	{ 136, 0, 33072, 1, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 113, 0, 1, 2, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
	{ 220, 168, 1, 2, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
};
static unsigned char IMCMUSIC_ChannelVol[8] = { 100, 97, 99, 100, 100, 100, 100, 100 };
static const unsigned char IMCMUSIC_ChannelEnvCounter[8] = { 0, 3, 4, 0, 0, 0, 0, 0 };
static const bool IMCMUSIC_ChannelStopNote[8] = { true, true, true, false, false, false, false, false };
TImcSongData imcDataIMCMUSIC = {
	/*LEN*/ 0xA, /*ROWLENSAMPLES*/ 8268, /*ENVLISTSIZE*/ 9, /*ENVCOUNTERLISTSIZE*/ 10, /*OSCLISTSIZE*/ 13, /*EFFECTLISTSIZE*/ 5, /*VOL*/ 40,
	IMCMUSIC_OrderTable, IMCMUSIC_PatternData, IMCMUSIC_PatternLookupTable, IMCMUSIC_EnvList, IMCMUSIC_EnvCounterList, IMCMUSIC_OscillatorList, IMCMUSIC_EffectList,
	IMCMUSIC_ChannelVol, IMCMUSIC_ChannelEnvCounter, IMCMUSIC_ChannelStopNote };

#endif
