#include "pong.h"
#include "timer.h"
#include "stdint.h"
#include "keyboard.h"
#include "framebuffer.h"
#include "lib.h"
#include "stdbool.h"

#define PADDLE_WIDTH 10
#define PADDLE_HEIGHT 60
#define BALL_SIZE 8
#define PADDLE_SPEED 15
#define BALL_SPEED_X 10
#define BALL_SPEED_Y 8

static int player_y = 0;
static int ai_y = 0;
static int ball_x = 0;
static int ball_y = 0;
static int ball_dx = BALL_SPEED_X;
static int ball_dy = BALL_SPEED_Y;

static int screen_width = 0;
static int screen_height = 0;

static int player_score = 0;
static int ai_score = 0;

static bool kill_sig = false;

static void pong_reset_ball()
{
	ball_x = screen_width / 2 - BALL_SIZE / 2;
	ball_y = screen_height / 2 - BALL_SIZE / 2;
	ball_dx = (ball_dx > 0) ? -BALL_SPEED_X : BALL_SPEED_X;
	ball_dy = BALL_SPEED_Y;
}

static void pong_reset()
{
	player_y = screen_height / 2 - PADDLE_HEIGHT / 2;
	ai_y = screen_height / 2 - PADDLE_HEIGHT / 2;
	player_score = 0;
	ai_score = 0;
	pong_reset_ball();
	kill_sig = false;
}

static void pong_update()
{
	Key key = keyboard_get_key();
	if (key == ESCAPE)
	{
		kill_sig = true;
		return;
	}

	if (key == W)
	{
		player_y -= PADDLE_SPEED;
	}
	else if (key == S)
	{
		player_y += PADDLE_SPEED;
	}

	// AI logic
	if (ball_y < ai_y + PADDLE_HEIGHT / 2)
	{
		ai_y -= PADDLE_SPEED - 5; // AI is slightly slower
	}
	else
	{
		ai_y += PADDLE_SPEED - 5;
	}

	// Constrain paddles
	if (player_y < 0) player_y = 0;
	if (player_y > screen_height - PADDLE_HEIGHT) player_y = screen_height - PADDLE_HEIGHT;
	if (ai_y < 0) ai_y = 0;
	if (ai_y > screen_height - PADDLE_HEIGHT) ai_y = screen_height - PADDLE_HEIGHT;

	// Move ball
	ball_x += ball_dx;
	ball_y += ball_dy;

	// Ball collision with top/bottom
	if (ball_y <= 0 || ball_y >= screen_height - BALL_SIZE)
	{
		ball_dy = -ball_dy;
	}

	// Ball collision with paddles
	// Left paddle
	if (ball_x <= PADDLE_WIDTH && ball_y + BALL_SIZE >= player_y && ball_y <= player_y + PADDLE_HEIGHT)
	{
		ball_dx = -ball_dx;
		ball_x = PADDLE_WIDTH;
	}
	// Right paddle
	if (ball_x >= screen_width - PADDLE_WIDTH - BALL_SIZE && ball_y + BALL_SIZE >= ai_y && ball_y <= ai_y + PADDLE_HEIGHT)
	{
		ball_dx = -ball_dx;
		ball_x = screen_width - PADDLE_WIDTH - BALL_SIZE;
	}

	// Score
	if (ball_x < 0)
	{
		ai_score++;
		pong_reset_ball();
	}
	else if (ball_x > screen_width)
	{
		player_score++;
		pong_reset_ball();
	}
}

static void pong_render()
{
	framebuffer_clear(0x000000);

	// Draw player paddle
	framebuffer_draw_rect(0, player_y, PADDLE_WIDTH, PADDLE_HEIGHT, 0xFFFFFF);

	// Draw AI paddle
	framebuffer_draw_rect(screen_width - PADDLE_WIDTH, ai_y, PADDLE_WIDTH, PADDLE_HEIGHT, 0xFFFFFF);

	// Draw ball
	framebuffer_draw_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, 0xFFFFFF);

	// Draw scores
	char score_buf[16];
	to_string(player_score, score_buf);
	framebuffer_put_string(score_buf, (screen_width / 4) / framebuffer_get_font_width(), 2, 0xFFFFFF, 0x000000);

	to_string(ai_score, score_buf);
	framebuffer_put_string(score_buf, (3 * screen_width / 4) / framebuffer_get_font_width(), 2, 0xFFFFFF, 0x000000);

	framebuffer_swap();
}

void pong_init()
{
	screen_width = framebuffer_get_width();
	screen_height = framebuffer_get_height();

	pong_reset();

	framebuffer_set_double_buffer(true);
	framebuffer_clear(0x000000);

	uint64_t start_time = timer_get_ticks();

	while (1)
	{
		if (kill_sig)
		{
			framebuffer_set_double_buffer(false);
			return;
		}
		if (timer_get_ticks() - start_time >= 1) // Faster than snake
		{
			start_time = timer_get_ticks();
			pong_update();
			pong_render();
		}
	}
}
