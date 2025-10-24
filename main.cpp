#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>

#ifdef _WIN32
  #include <windows.h>
#endif
#ifdef __APPLE__
  #include <GLUT/glut.h>
  #include <OpenGL/glu.h>
#else
  #include <GL/glut.h>
  #include <GL/glu.h>
#endif

// Utility Math Functions
template <typename T>
static inline T clampv(T v, T lo, T hi){ return (v < lo) ? lo : (v > hi) ? hi : v; }

struct Vec2 { float x=0.f, y=0.f; };
static inline Vec2 operator+(Vec2 a, Vec2 b){ return {a.x+b.x, a.y+b.y}; }
static inline Vec2 operator-(Vec2 a, Vec2 b){ return {a.x-b.x, a.y-b.y}; }
static inline Vec2 operator*(Vec2 a, float s){ return {a.x*s, a.y*s}; }
static inline float dot(Vec2 a, Vec2 b){ return a.x*b.x + a.y*b.y; }
static inline float length(Vec2 a){ return std::sqrt(dot(a,a)); }
static inline Vec2 normalize(Vec2 a){ float L=length(a); return (L>1e-6f)? Vec2{a.x/L,a.y/L} : Vec2{1.f,0.f}; }

static int scrW=900, scrH=700;
static float nowSec(){ return glutGet(GLUT_ELAPSED_TIME)/1000.0f; }
static std::mt19937 rng(1234567u);
static std::uniform_real_distribution<float> u01(0.f,1.f);

// --- Drawing Functions for Modern Filled UI ---

static void drawRectFilled(float cx,float cy,float w,float h){
  float x0=cx-w/2.f, x1=cx+w/2.f, y0=cy-h/2.f, y1=cy+h/2.f;
  glBegin(GL_QUADS);
  glVertex2f(x0,y0); glVertex2f(x1,y0); glVertex2f(x1,y1); glVertex2f(x0,y1);
  glEnd();
}

static void drawCircleFilled(float cx,float cy,float r,int seg=32){
  glBegin(GL_TRIANGLE_FAN);
  glVertex2f(cx,cy);
  for(int i=0;i<=seg;i++){ float th=(float)i*(float)(2.0*M_PI)/seg;
    glVertex2f(cx+cosf(th)*r, cy+sinf(th)*r); }
  glEnd();
}

static void drawText(float x,float y,const std::string& s, void* font=GLUT_BITMAP_HELVETICA_18){
  glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
  glRasterPos2f(x,y); for(size_t i=0;i<s.size();++i) glutBitmapCharacter(font, s[i]);
  glPopMatrix();
}

// --- Game Structures and State ---
enum Screen { MENU, PLAY, PAUSE, HELP, HIGHSCORES, WIN, GAMEOVER };
enum PerkType {
  EXTRA_LIFE, SPEED_UP, WIDE_PADDLE, SHRINK_PADDLE,
  THROUGH_BALL, FIREBALL, INSTANT_DEATH, SHOOTING_PADDLE
};

struct Brick { float x,y,w,h; bool alive; int hp; float r,g,b; int score; };
struct Perk  { Vec2 pos, vel; float size; PerkType type; bool alive; };
struct Bullet{ Vec2 pos, vel; float w,h; bool alive; };

struct Ball {
  Vec2 pos, vel; float speed, radius;
  bool stuck;
  bool through; float throughTimer;
  bool fireball; float fireballTimer;
};
struct Paddle {
  Vec2 pos; float w,h; float speed;
  float widthTimer;
  bool shooting; float shootingTimer;
};

static Screen current = MENU;
static std::vector<Brick>  bricks;
static std::vector<Perk>   perks;
static std::vector<Bullet> bullets;
static Ball    ball;
static Paddle paddle;
static int     lives=3, score=0;
static float   startTime=0.f, playTime=0.f, lastTick=0.f;
static bool    leftHeld=false, rightHeld=false, hasLaunched=false;
static bool    canResume=false;
static int     menuIndex=0;
static float   globalSpeedGain=0.f;

static bool    haveBest=false; static int bestScore=0; static float bestTime=0.f;
static int     pauseMenuIndex = 0; // 0 = Resume, 1 = Exit to Menu

// In-memory run history (no file I/O)
struct Run { float t; int s; };
static std::vector<Run> history;
static const int MAX_LIVES = 5;

static void saveHighScore(){
  history.push_back({playTime, score});
}
static void loadBest(){
  haveBest = false; bestScore = 0; bestTime = 0.f;
  for(const auto& r : history){
    if(!haveBest || r.s > bestScore || (r.s == bestScore && r.t < bestTime)){
      haveBest = true; bestScore = r.s; bestTime = r.t;
    }
  }
}

static void resetBallOnPaddle(){
  ball.stuck=true; hasLaunched=false;
  ball.through=false; ball.throughTimer=0.f;
  ball.fireball=false; ball.fireballTimer=0.f;
  ball.speed = 320.f + globalSpeedGain;
  ball.pos = {paddle.pos.x, paddle.pos.y + paddle.h/2.f + ball.radius + 1.f};
  ball.vel = {0.f, 1.f};
}

static void buildBricks(int rows=7,int cols=12){
  bricks.clear();
  float marginX=70.f, marginY=100.f, gap=6.f;
  float areaW = scrW - 2*marginX;
  float bw = (areaW - (cols-1)*gap)/cols;
  float bh = 22.f;

  float colors[7][3] = {
    {0.9f, 0.2f, 0.4f}, {0.9f, 0.6f, 0.1f}, {0.9f, 0.9f, 0.2f},
    {0.2f, 0.8f, 0.4f}, {0.2f, 0.6f, 0.9f}, {0.5f, 0.3f, 0.9f},
    {0.8f, 0.8f, 0.8f}
  };

  for(int r=0;r<rows;r++){
    for(int c=0;c<cols;c++){
      Brick b;
      b.x = marginX + c*(bw+gap) + bw/2.f;
      b.y = scrH - marginY - r*(bh+gap) - bh/2.f;
      b.w=bw; b.h=bh; b.alive=true; b.hp = (r<2?2:1);
      b.r = colors[r % 7][0]; b.g = colors[r % 7][1]; b.b = colors[r % 7][2];
      b.score = 50 + 10*r;
      bricks.push_back(b);
    }
  }
}

static void newGame(){
  score=0; lives=3; globalSpeedGain=0.f; perks.clear(); bullets.clear();
  paddle.pos={scrW/2.f, 48.f}; paddle.w=120.f; paddle.h=16.f;
  paddle.speed=630.f; paddle.widthTimer=0.f; paddle.shooting=false; paddle.shootingTimer=0.f;
  ball.radius=9.f; ball.speed=320.f; ball.stuck=true; ball.through=false; ball.fireball=false;
  resetBallOnPaddle();
  buildBricks();
  startTime = nowSec(); lastTick=startTime; playTime=0.f;
  current=PLAY; canResume=true;
}

// Exit to main menu handler: clear play-state and return to menu
static void exitToMenu(){
  perks.clear(); bullets.clear();
  // reset some gameplay state
  bricks.clear();
  score = 0;
  lives = 3;
  globalSpeedGain = 0.f;
  paddle.pos = {scrW/2.f, 48.f}; paddle.w = 120.f; paddle.h = 16.f;
  ball.radius = 9.f; ball.speed = 320.f;
  resetBallOnPaddle();
  canResume = false;
  current = MENU;
  pauseMenuIndex = 0;
}

static void maybeSpawnPerk(const Brick& b){
  float p=0.22f; if(u01(rng)<p){
    Perk pk; pk.pos={b.x,b.y}; pk.vel={0,-150.f}; pk.size=18.f; pk.alive=true;
    float r=u01(rng);
    if(r<0.18f) pk.type=EXTRA_LIFE;
    else if(r<0.36f) pk.type=SPEED_UP;
    else if(r<0.52f) pk.type=WIDE_PADDLE;
    else if(r<0.66f) pk.type=SHRINK_PADDLE;
    else if(r<0.78f) pk.type=THROUGH_BALL;
    else if(r<0.90f) pk.type=FIREBALL;
    else if(r<0.96f) pk.type=SHOOTING_PADDLE;
    else pk.type=INSTANT_DEATH;
    perks.push_back(pk);
  }
}

static void applyPerk(PerkType t){
  switch(t){
    case EXTRA_LIFE:      lives = (lives<MAX_LIVES? lives+1:MAX_LIVES); break;
    case SPEED_UP:        ball.speed *= 1.18f;                     break;
    case WIDE_PADDLE:     paddle.w = (paddle.w*1.35f<320.f? paddle.w*1.35f:320.f); paddle.widthTimer=14.f; break;
    case SHRINK_PADDLE:   paddle.w = (paddle.w*0.7f>60.f?  paddle.w*0.7f:60.f);  paddle.widthTimer=12.f; break;
    case THROUGH_BALL:    ball.through=true; ball.throughTimer=10.f; break;
    case FIREBALL:        ball.fireball=true; ball.fireballTimer=8.f; ball.through=true; if(ball.throughTimer<8.f) ball.throughTimer=8.f; break;
    case INSTANT_DEATH:   lives = 0; current=GAMEOVER; saveHighScore(); canResume=false; break;
    case SHOOTING_PADDLE: paddle.shooting=true; paddle.shootingTimer=12.f; break;
  }
}
// Collision detection: Axis-Aligned Bounding Box (AABB) vs Circle
static bool aabbCircleCollision(float rx,float ry,float rw,float rh, Vec2 c,float r, Vec2* nrm,float* pen){
  float cx = clampv(c.x, rx-rw/2.f, rx+rw/2.f);
  float cy = clampv(c.y, ry-rh/2.f, ry+rh/2.f);
  float dx = c.x - cx, dy = c.y - cy;
  float d2 = dx*dx + dy*dy; if(d2 > r*r) return false;
  float d = std::sqrt(d2<1e-6f?1e-6f:d2);
  if(nrm){ if(d>1e-4f) *nrm = {dx/d, dy/d}; else *nrm = {0.f,1.f}; }
  if(pen) *pen = r - d;
  return true;
}
static void reflectBall(Vec2 n){
  Vec2 v=ball.vel; float sp=length(v); if(sp<1e-6f) return;
  Vec2 dir = v*(1.f/sp);
  Vec2 r   = dir - n*(2.f*dot(dir,n));
  ball.vel = normalize(r) * ball.speed;
}

static void loseLife(){
  if(lives > 0) lives--;
  if(lives <= 0){
    lives = 0;
    current=GAMEOVER; saveHighScore(); canResume=false;
  } else {
    paddle.pos.x = scrW/2.f; paddle.w=120.f; paddle.widthTimer=0.f; paddle.shooting=false; paddle.shootingTimer=0.f;
    resetBallOnPaddle();
  }
}

static void fireBullet(){
  if(!paddle.shooting) return;
  Bullet b; b.pos={paddle.pos.x, paddle.pos.y + paddle.h/2.f + 8.f}; b.vel={0,640.f}; b.w=4.f; b.h=10.f; b.alive=true;
  bullets.push_back(b);
}

// --- Game Logic Update ---
static void updateGame(float dt){
  // Update Timers and Speed
  globalSpeedGain += dt*2.f; ball.speed += dt*4.f;
  if(ball.through){ ball.throughTimer -= dt; if(ball.throughTimer<=0){ ball.through=false; } }
  if(ball.fireball){ ball.fireballTimer -= dt; if(ball.fireballTimer<=0){ ball.fireball=false; } }
  if(paddle.widthTimer>0){ paddle.widthTimer -= dt; if(paddle.widthTimer<=0){ paddle.widthTimer=0; paddle.w=120.f; } }
  if(paddle.shooting){ paddle.shootingTimer -= dt; if(paddle.shootingTimer<=0){ paddle.shooting=false; } }

  // Update Paddle Movement
  float vx=0.f; if(leftHeld) vx -= paddle.speed; if(rightHeld) vx += paddle.speed;
  paddle.pos.x += vx*dt;
  paddle.pos.x = clampv(paddle.pos.x, paddle.w/2.f+6.f, scrW - paddle.w/2.f - 6.f);

  // Update Ball Movement
  if(ball.stuck){
    ball.pos.x = paddle.pos.x;
    ball.pos.y = paddle.pos.y + paddle.h/2.f + ball.radius + 1.f;
  } else {
    ball.pos = ball.pos + ball.vel*dt;

    // Wall reflection
    if(ball.pos.x - ball.radius < 0){ ball.pos.x = ball.radius; ball.vel.x = std::fabs(ball.vel.x); }
    if(ball.pos.x + ball.radius > scrW){ ball.pos.x = scrW - ball.radius; ball.vel.x = -std::fabs(ball.vel.x); }
    if(ball.pos.y + ball.radius > scrH){ ball.pos.y = scrH - ball.radius; ball.vel.y = -std::fabs(ball.vel.y); }

    // Bottom boundary (lose life)
    if(ball.pos.y - ball.radius < 0){ loseLife(); return; }

    // Paddle Collision
    Vec2 n; float pen;
    if(aabbCircleCollision(paddle.pos.x,paddle.pos.y,paddle.w,paddle.h, ball.pos, ball.radius, &n,&pen)){
      ball.pos = ball.pos + n*pen;
      // Angle reflection based on hit position
      float rel = (ball.pos.x - paddle.pos.x) / (paddle.w/2.f); rel = clampv(rel,-1.f,1.f);
      Vec2 dir = normalize(Vec2{rel, 1.2f});
      ball.vel = dir * ball.speed; ball.vel.y = std::fabs(ball.vel.y);
    }

    // Brick Collision
    for(size_t i=0;i<bricks.size();++i){
      Brick& b = bricks[i]; if(!b.alive) continue;
      Vec2 bn; float bpen;
      if(aabbCircleCollision(b.x,b.y,b.w,b.h, ball.pos, ball.radius, &bn,&bpen)){
        int before=b.hp; b.hp-=1; score += b.score;
        if(before>0 && b.hp<=0){ b.alive=false; maybeSpawnPerk(b); }
        if(!(ball.through || ball.fireball)){ ball.pos = ball.pos + bn*bpen; reflectBall(bn); }
      }
    }
  }

  // Perk Movement and Collection
  for(size_t i=0;i<perks.size();++i){
    Perk& p=perks[i]; if(!p.alive) continue;
    p.pos = p.pos + p.vel*dt;
    if(p.pos.y < -30.f){ p.alive=false; continue; }
    if(std::fabs(p.pos.x - paddle.pos.x) <= (paddle.w/2.f + p.size/2.f) &&
       std::fabs(p.pos.y - paddle.pos.y) <= (paddle.h/2.f + p.size/2.f)){
      p.alive=false; applyPerk(p.type); if(lives<=0){ return; }
    }
  }

  // Bullet Movement and Collision
  for(size_t i=0;i<bullets.size();++i){
    Bullet& bu = bullets[i]; if(!bu.alive) continue;
    bu.pos = bu.pos + bu.vel*dt;
    if(bu.pos.y > scrH+20.f){ bu.alive=false; continue; }
    for(size_t j=0;j<bricks.size();++j){
      Brick& br = bricks[j]; if(!br.alive) continue;
      if(std::fabs(bu.pos.x - br.x) <= (br.w/2.f) && std::fabs(bu.pos.y - br.y) <= (br.h/2.f)){
        bu.alive=false; int before=br.hp; br.hp-=1; score += br.score;
        if(before>0 && br.hp<=0){ br.alive=false; maybeSpawnPerk(br); }
        break;
      }
    }
  }

  // Check for Win Condition
  bool any=false; for(size_t i=0;i<bricks.size();++i){ if(bricks[i].alive){ any=true; break; } }
  if(!any){ current=WIN; saveHighScore(); canResume=false; }
}
