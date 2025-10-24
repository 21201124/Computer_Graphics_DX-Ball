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
// --- Rendering Functions for Modern Filled UI ---

static void drawPerkIcon(PerkType t,float x,float y,float s){
  glPushMatrix(); glTranslatef(x,y,0); glScalef(s,s,1);
  glBegin(GL_LINES);
  switch(t){
    case EXTRA_LIFE: glColor3f(1.0f,0.2f,0.2f);
      glVertex2f(-0.5f, 0.2f); glVertex2f( 0.0f, 0.8f);
      glVertex2f( 0.5f, 0.2f); glVertex2f( 0.0f, 0.8f);
      glVertex2f(-0.5f, 0.2f); glVertex2f( 0.5f, 0.2f);
      break;
    case SPEED_UP: glColor3f(0.9f,0.9f,0.2f);
      glVertex2f(-0.5f, -0.5f); glVertex2f( 0.0f, 0.5f);
      glVertex2f( 0.5f, -0.5f); glVertex2f( 0.0f, 0.5f);
      glVertex2f(-0.3f, 0.0f); glVertex2f( 0.3f, 0.0f);
      break;
    case WIDE_PADDLE: glColor3f(0.3f,1.0f,0.3f); glVertex2f(-0.9f, 0.0f); glVertex2f( 0.9f, 0.0f); break;
    case SHRINK_PADDLE: glColor3f(1.0f,0.5f,0.1f); glVertex2f(-0.4f, 0.0f); glVertex2f( 0.4f, 0.0f); break;
    case THROUGH_BALL: glColor3f(0.2f,0.8f,1.0f);
      glVertex2f(0.0f, 0.8f); glVertex2f(-0.8f, 0.0f);
      glVertex2f(-0.8f, 0.0f); glVertex2f(0.0f, -0.8f);
      glVertex2f(0.0f, -0.8f); glVertex2f(0.8f, 0.0f);
      glVertex2f(0.8f, 0.0f); glVertex2f(0.0f, 0.8f);
      break;
    case FIREBALL: glColor3f(1.0f,0.4f,0.0f);
      glVertex2f(-0.5f, -0.5f); glVertex2f( 0.5f, 0.5f);
      glVertex2f( 0.5f, -0.5f); glVertex2f(-0.5f, 0.5f);
      break;
    case INSTANT_DEATH: glColor3f(0.8f,0.0f,0.8f);
      glVertex2f(-0.6f, 0.6f); glVertex2f( 0.6f, -0.6f);
      glVertex2f( 0.6f, 0.6f); glVertex2f(-0.6f, -0.6f);
      break;
    case SHOOTING_PADDLE: glColor3f(0.9f,0.9f,0.2f);
      glVertex2f(0.0f, -0.5f); glVertex2f( 0.0f, 0.5f);
      glVertex2f(-0.3f, 0.5f); glVertex2f( 0.3f, 0.5f);
      break;
  }
  glEnd();
  glPopMatrix();
}

static void renderHUD(){
  glColor3f(0.9f, 0.9f, 0.9f);
  drawText(10, scrH-24, std::string("SCORE: ")+std::to_string(score));
  drawText(10, scrH-48, std::string("LIVES: ")+std::to_string(lives));

  float tNow=nowSec(); if(current==PLAY) playTime += (tNow-lastTick); lastTick=tNow;
  char buf[64]; std::snprintf(buf,sizeof(buf),"TIME: %.1fs", playTime);
  drawText(scrW-160, scrH-24, buf);

  int y = scrH-72; char pbuf[64];
  glColor3f(1.0f, 0.9f, 0.2f);
  if(ball.through){ std::snprintf(pbuf,sizeof(pbuf),"THROUGH: %ds", (int)std::ceil(ball.throughTimer)); drawText(scrW-200,y,pbuf); y-=22; }
  if(ball.fireball){ std::snprintf(pbuf,sizeof(pbuf),"FIREBALL: %ds", (int)std::ceil(ball.fireballTimer)); drawText(scrW-200,y,pbuf); y-=22; }
  if(paddle.shooting){ std::snprintf(pbuf,sizeof(pbuf),"SHOOTING: %ds", (int)std::ceil(paddle.shootingTimer)); drawText(scrW-200,y,pbuf); y-=22; }
}

static void renderScene(){
  glClearColor(0.05f,0.05f,0.08f,1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // MENU
  if(current==MENU){
    glColor3f(0.2f, 0.8f, 1.0f);
    drawText(scrW/2.f-130, scrH-120, "DX-BALL [MODERN EDITION]");
    const char* itemsResume[] = {"[ RESUME ]","[ START NEW GAME ]","[ HIGH SCORES ]","[ HELP ]","[ EXIT ]"};
    const char* itemsFresh[]  = {"[ START NEW GAME ]","[ HIGH SCORES ]","[ HELP ]","[ EXIT ]"};
    const char** items = canResume ? itemsResume : itemsFresh;
    int itemCount = canResume ? 5 : 4;
    for(int i=0;i<itemCount;i++){
      float y = scrH/2.f + 60 - i*40.f;
      if(i==menuIndex){ glColor3f(1.0f,0.9f,0.2f); drawText(scrW/2.f-90, y, std::string("> ")+items[i]); }
      else { glColor3f(0.2f, 0.8f, 1.0f); drawText(scrW/2.f-70, y, items[i]); }
    }
    loadBest();
    if(haveBest){
      char b[96]; std::snprintf(b,sizeof(b),"BEST: %d PTS IN %.1FS", bestScore, bestTime);
      glColor3f(0.3f,1.0f,0.3f); drawText(scrW/2.f-130, scrH/2.f-140, b);
    }
    glutSwapBuffers(); return;
  }

  // HELP
  if(current==HELP){
    glColor3f(0.5f, 0.7f, 1.0f);
    drawText(40, scrH-100, "HELP / CONTROLS:");
    drawText(40, scrH-130, "MOUSE OR LEFT/RIGHT ARROW TO MOVE PADDLE");
    drawText(40, scrH-155, "SPACE / LEFT CLICK: LAUNCH BALL");
    drawText(40, scrH-180, "P OR ESC: PAUSE/RESUME");
    drawText(40, scrH-205, "F OR RIGHT CLICK: FIRE BULLET (WHEN SHOOTING ACTIVE)");
    drawText(40, scrH-235, "PERKS: LIFE(HEART), SPEED(BOLT), WIDE/SMALL PADDLE, THROUGH(RING),");
    drawText(40, scrH-255, "      FIRE(FLAME), DEATH(SKULL), SHOOT(GUN)");
    drawText(40, scrH-285, "GOAL: CLEAR ALL BRICKS AS FAST AS POSSIBLE.");
    glColor3f(1.0f,0.9f,0.2f); drawText(40, scrH-315, "PRESS ENTER TO RETURN TO MENU.");
    glutSwapBuffers(); return;
  }

  // HIGHSCORES
  if(current==HIGHSCORES){
    glColor3f(0.5f, 0.7f, 1.0f);
    drawText(40, scrH-90, "HIGH SCORES (SCORE, TIME)");

    std::vector<Run> rows = history;
    std::sort(rows.begin(), rows.end(), [](const Run& a, const Run& b){
      if(a.s != b.s) return a.s > b.s;
      return a.t < b.t;
    });

    int y = scrH-130; int shown=0;
    if(rows.empty()){
      drawText(60, y, "NO SCORES YET");
    } else {
      for(size_t i=0;i<rows.size() && shown<15;i++){
        char row[96];
        std::snprintf(row,sizeof(row),"%2d) %6d PTS    %6.1FS", (int)i+1, rows[i].s, rows[i].t);
        drawText(60, y, row); y -= 24; ++shown;
      }
    }

    loadBest();
    if(haveBest){
      char b[96]; std::snprintf(b,sizeof(b),"BEST: %d PTS IN %.1FS", bestScore, bestTime);
      glColor3f(0.3f,1.0f,0.3f); drawText(40, y-20, b);
      glColor3f(0.5f, 0.7f, 1.0f);
    }

    glColor3f(1.0f,0.9f,0.2f); drawText(40, 60, "PRESS ENTER FOR MENU");
    glutSwapBuffers(); return;
  }

  // GAME PLAY: draw bricks, paddle, ball, perks, bullets
  for(size_t i=0;i<bricks.size();++i){
    const Brick& b=bricks[i]; if(!b.alive) continue;
    float multiplier = (b.hp == 2) ? 1.0f : 0.6f;
    glColor3f(b.r * multiplier, b.g * multiplier, b.b * multiplier);
    drawRectFilled(b.x, b.y, b.w, b.h);

    glColor3f(0.1f, 0.1f, 0.1f);
    float x0 = b.x - b.w/2.f, x1 = b.x + b.w/2.f;
    float y0 = b.y - b.h/2.f, y1 = b.y + b.h/2.f;
    glBegin(GL_LINE_LOOP);
      glVertex2f(x0,y0); glVertex2f(x1,y0); glVertex2f(x1,y1); glVertex2f(x0,y1);
    glEnd();
  }

  glColor3f(0.2f, 0.5f, 0.9f);
  drawRectFilled(paddle.pos.x, paddle.pos.y, paddle.w, paddle.h);

  if(ball.fireball) glColor3f(1.0f,0.45f,0.15f);
  else if(ball.through) glColor3f(0.9f,0.2f,1.0f);
  else glColor3f(0.3f, 1.0f, 0.3f);
  drawCircleFilled(ball.pos.x, ball.pos.y, ball.radius, 32);

  for(size_t i=0;i<perks.size();++i){
    const Perk& p=perks[i]; if(!p.alive) continue;
    glColor3f(0.8f, 0.8f, 0.8f);
    drawRectFilled(p.pos.x, p.pos.y, p.size, p.size);
    drawPerkIcon(p.type, p.pos.x, p.pos.y, 8.f);
  }

  for(size_t i=0;i<bullets.size();++i){
    const Bullet& bu = bullets[i]; if(!bu.alive) continue;
    glColor3f(1.0f, 0.9f, 0.2f);
    drawRectFilled(bu.pos.x, bu.pos.y, bu.w, bu.h);
  }

  renderHUD();

  // PAUSE SCREEN WITH OPTIONS
  if(current==PAUSE){
    glColor3f(0.9f,0.9f,0.9f);
    drawText(scrW/2.f-40, scrH/2.f + 60, "== PAUSED ==");

    // Options: Resume, Exit to Main Menu
    const char* opts[] = {"[ RESUME ]", "[ EXIT TO MAIN MENU ]"};
    for(int i=0;i<2;i++){
      if(i==pauseMenuIndex) glColor3f(1.0f,0.9f,0.2f);
      else glColor3f(0.6f,0.8f,1.0f);
      float y = scrH/2.f + 20 - i*40.f;
      drawText(scrW/2.f - (i==0?50:140), y, opts[i]);
    }
    drawText(scrW/2.f-140, scrH/2.f - 120, "Use UP/DOWN to select, ENTER or Left-Click to confirm.");
  }

  if(current==WIN){ glColor3f(0.3f,1.0f,0.3f); drawText(scrW/2.f-80, scrH/2.f, "[ LEVEL CLEARED! ]"); drawText(scrW/2.f-120, scrH/2.f-30, "PRESS ENTER FOR MENU"); }
  if(current==GAMEOVER){ glColor3f(1.0f,0.3f,0.3f); drawText(scrW/2.f-60, scrH/2.f, "[ GAME OVER ]"); drawText(scrW/2.f-120, scrH/2.f-30, "PRESS ENTER FOR MENU"); }

  glutSwapBuffers();
}

// --- GLUT Callbacks ---

static void onDisplay(){ renderScene(); }

static void onIdle(){
  if(current==PLAY){
    static float prev = nowSec();
    float t = nowSec(); float dt = t - prev; prev = t;
    if(dt<0.f) dt=0.f; if(dt>0.03f) dt=0.03f;
    updateGame(dt);
  }
  glutPostRedisplay();
}

static void onReshape(int w,int h){
  scrW=w; scrH=h; glViewport(0,0,w,h);
  glMatrixMode(GL_PROJECTION); glLoadIdentity();
  gluOrtho2D(0, (GLdouble)w, 0, (GLdouble)h);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

static void onKey(unsigned char key, int, int){
  // Top-level MENU input
  if(current==MENU){
    if(key=='\r' || key=='\n'){
      const char* itemsResume[] = {"[ RESUME ]","[ START NEW GAME ]","[ HIGH SCORES ]","[ HELP ]","[ EXIT ]"};
      const char* itemsFresh[]  = {"[ START NEW GAME ]","[ HIGH SCORES ]","[ HELP ]","[ EXIT ]"};
      const char** items = canResume ? itemsResume : itemsFresh;
      int itemCount = canResume ? 5 : 4;
      if(menuIndex>=0 && menuIndex<itemCount){
        std::string it = items[menuIndex];
        if(it=="[ RESUME ]" && canResume) current=PLAY;
        else if(it=="[ START NEW GAME ]") newGame();
        else if(it=="[ HIGH SCORES ]") current=HIGHSCORES;
        else if(it=="[ HELP ]") current=HELP;
        else if(it=="[ EXIT ]") std::exit(0);
      }
    }
    if(key==27) std::exit(0);
    return;
  }

  // HELP or HIGHSCORES return to menu
  if(current==HELP || current==HIGHSCORES){
    if(key=='\r' || key=='\n' || key==27) current=MENU;
    return;
  }

  // WIN or GAMEOVER: Enter -> menu
  if(current==WIN || current==GAMEOVER){
    if(key=='\r' || key=='\n') current=MENU;
    return;
  }

  // Toggle Pause (P or Esc)
  if(key==27 || key=='p' || key=='P'){
    if(current==PLAY){ current=PAUSE; canResume=true; pauseMenuIndex=0; }
    else if(current==PAUSE){ current=PLAY; }
    return;
  }

  // If paused, allow keyboard selection
  if(current==PAUSE){
    if(key=='\r' || key=='\n'){
      if(pauseMenuIndex==0){ current=PLAY; }
      else if(pauseMenuIndex==1){ exitToMenu(); }
    }
    // also support single-key shortcuts
    if(key=='r' || key=='R'){ current=PLAY; }
    if(key=='e' || key=='E'){ exitToMenu(); }
    return;
  }

  if(current!=PLAY) return;

  // Launch Ball
  if(key==' ' && ball.stuck){
    ball.stuck=false; ball.vel = normalize(Vec2{0.2f,1.f})*ball.speed; hasLaunched=true;
  }
  // Fire Bullet
  if(key=='f' || key=='F') fireBullet();
}

static void onSpKey(int key,int,int){
  // Menu navigation
  if(current==MENU){
    int itemCount = canResume ? 5 : 4;
    if(key==GLUT_KEY_UP){ menuIndex = (menuIndex - 1 + itemCount) % itemCount; }
    if(key==GLUT_KEY_DOWN){ menuIndex = (menuIndex + 1) % itemCount; }
    return;
  }
// Pause menu navigation
  if(current==PAUSE){
    if(key==GLUT_KEY_UP){ pauseMenuIndex = (pauseMenuIndex - 1 + 2) % 2; }
    if(key==GLUT_KEY_DOWN){ pauseMenuIndex = (pauseMenuIndex + 1) % 2; }
    return;
  }
  // In gameplay, movement keys
  if(current!=PLAY) return;
  if(key==GLUT_KEY_LEFT) leftHeld=true;
  if(key==GLUT_KEY_RIGHT) rightHeld=true;
}

static void onSpKeyUp(int key,int,int){
  if(key==GLUT_KEY_LEFT) leftHeld=false;
  if(key==GLUT_KEY_RIGHT) rightHeld=false;
}

static void onMouse(int button,int state,int x,int y){
  // Convert GLUT y to window coords (we use 0..scrH)
  int wy = scrH - y;
  if(current==MENU){
    if(button==GLUT_LEFT_BUTTON && state==GLUT_DOWN){
      const char* itemsResume[] = {"[ RESUME ]","[ START NEW GAME ]","[ HIGH SCORES ]","[ HELP ]","[ EXIT ]"};
      const char* itemsFresh[]  = {"[ START NEW GAME ]","[ HIGH SCORES ]","[ HELP ]","[ EXIT ]"};
      const char** items = canResume ? itemsResume : itemsFresh;
      int itemCount = canResume ? 5 : 4;
      if(menuIndex>=0 && menuIndex<itemCount){
        std::string it = items[menuIndex];
        if(it=="[ RESUME ]" && canResume) current=PLAY;
        else if(it=="[ START NEW GAME ]") newGame();
        else if(it=="[ HIGH SCORES ]") current=HIGHSCORES;
        else if(it=="[ HELP ]") current=HELP;
        else if(it=="[ EXIT ]") std::exit(0);
      }
    }
    return;
  }

  if(current==PAUSE){
    if(button==GLUT_LEFT_BUTTON && state==GLUT_DOWN){
      // Treat left-click as Enter: select highlighted option
      if(pauseMenuIndex==0){ current=PLAY; }
      else if(pauseMenuIndex==1){ exitToMenu(); }
    }
    return;
  }

  if(current==PLAY){
    if(ball.stuck && button==GLUT_LEFT_BUTTON && state==GLUT_DOWN){
      ball.stuck=false; ball.vel = normalize(Vec2{0,1})*ball.speed;
    }
    if(button==GLUT_RIGHT_BUTTON && state==GLUT_DOWN){
      fireBullet();
    }
  }
}

static void onMotion(int x,int y){ (void)y;
  if(current==PLAY){
    float minX = paddle.w/2.f+6.f, maxX = scrW - paddle.w/2.f - 6.f;
    float nx = (float)x; if(nx<minX) nx=minX; if(nx>maxX) nx=maxX;
    paddle.pos.x = nx;
  }
}
static void onPassiveMotion(int x,int y){ onMotion(x,y); }

int main(int argc,char** argv){
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
  glutInitWindowSize(scrW, scrH);
  glutCreateWindow("DX-Ball - OpenGL GLUT [Modern Edition]");
  glDisable(GL_DEPTH_TEST);

  glutDisplayFunc(onDisplay);
  glutIdleFunc(onIdle);
  glutReshapeFunc(onReshape);
  glutKeyboardFunc(onKey);
  glutSpecialFunc(onSpKey);
  glutSpecialUpFunc(onSpKeyUp);
  glutMouseFunc(onMouse);
  glutMotionFunc(onMotion);
  glutPassiveMotionFunc(onPassiveMotion);

  rng.seed((unsigned)time(nullptr));
  loadBest();

  menuIndex = 0; canResume = false; pauseMenuIndex = 0;

  // initialize default paddle/ball so menu can show something
  paddle.pos = {scrW/2.f, 48.f}; paddle.w = 120.f; paddle.h = 16.f; paddle.speed = 630.f;
  ball.radius = 9.f; ball.speed = 320.f; resetBallOnPaddle();

  glutMainLoop();
  return 0;
}
