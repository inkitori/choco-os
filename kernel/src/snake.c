#include "snake.h"
#include "timer.h"
#include "stdint.h"
#include "keyboard.h"
#include "framebuffer.h"
#include "lib.h"
#include "stdbool.h"

// super cursed no malloc code lol

static SnakeBody snake[100];
static SnakeBody prev_snake[100];
static Direction direction = DOWN;
static uint64_t snake_size = 1;

static uint64_t apple_x = 5;
static uint64_t apple_y = 5;

static bool just_ate_apple = false;
static bool kill_sig = false;

static uint32_t rng_seed = 12345;

uint64_t tail_x;
uint64_t tail_y;

static uint32_t get_random()
{
	rng_seed = rng_seed * 1664525 + 1013904223;
	return rng_seed;
}

static void snake_reset()
{
	direction = DOWN;
	snake_size = 1;
	kill_sig = false;
	just_ate_apple = false;

	snake[0].coordinates_x = 5;
	snake[0].coordinates_y = 5;

	uint8_t max_x = framebuffer_get_width() / GRID_SIZE;
	uint8_t max_y = framebuffer_get_height() / GRID_SIZE;
	if (max_x == 0) max_x = 1;
	if (max_y == 0) max_y = 1;

	apple_x = get_random() % max_x;
	apple_y = get_random() % max_y;
}

static void snake_update()
{
	uint8_t max_x = framebuffer_get_width() / GRID_SIZE;
	uint8_t max_y = framebuffer_get_height() / GRID_SIZE;
	if (max_x == 0) max_x = 1;
	if (max_y == 0) max_y = 1;

	uint8_t current_tail_x = snake[snake_size - 1].coordinates_x;
	uint8_t current_tail_y = snake[snake_size - 1].coordinates_y;

	if (just_ate_apple)
	{
		if (snake_size < 100)
			snake_size++;
		snake[snake_size - 1].coordinates_x = tail_x;
		snake[snake_size - 1].coordinates_y = tail_y;

		just_ate_apple = false;
	}

	for (uint8_t snake_idx = snake_size - 1; snake_idx > 0; snake_idx--)
	{
		snake[snake_idx].coordinates_x = snake[snake_idx - 1].coordinates_x;
		snake[snake_idx].coordinates_y = snake[snake_idx - 1].coordinates_y;
	}

	Key key = keyboard_get_key();
	if (key == ESCAPE)
	{
		kill_sig = true;
		return;
	}
	if (key == A && direction != RIGHT)
		direction = LEFT;
	else if (key == D && direction != LEFT)
		direction = RIGHT;
	else if (key == W && direction != DOWN)
		direction = UP;
	else if (key == S && direction != UP)
		direction = DOWN;

	if (direction == LEFT)
		snake[0].coordinates_x--;
	else if (direction == RIGHT)
		snake[0].coordinates_x++;
	else if (direction == UP)
		snake[0].coordinates_y--;
	else if (direction == DOWN)
		snake[0].coordinates_y++;

	// Wrap around
	if (snake[0].coordinates_x >= max_x)
	{
		if (direction == LEFT)
			snake[0].coordinates_x = max_x - 1;
		else
			snake[0].coordinates_x = 0;
	}
	if (snake[0].coordinates_y >= max_y)
	{
		if (direction == UP)
			snake[0].coordinates_y = max_y - 1;
		else
			snake[0].coordinates_y = 0;
	}

	// Self-collision
	for (uint64_t i = 1; i < snake_size; i++)
	{
		if (snake[0].coordinates_x == snake[i].coordinates_x &&
			snake[0].coordinates_y == snake[i].coordinates_y)
		{
			snake_reset();
			return;
		}
	}

	if (snake[0].coordinates_x == apple_x && snake[0].coordinates_y == apple_y)
	{
		apple_x = get_random() % max_x;
		apple_y = get_random() % max_y;

		just_ate_apple = true;

		tail_x = current_tail_x;
		tail_y = current_tail_y;
	}
}

static void snake_render()
{
	framebuffer_clear(0x000000);

	for (int snake_idx = 0; snake_idx < snake_size; snake_idx++)
	{
		framebuffer_draw_rect(snake[snake_idx].coordinates_x * GRID_SIZE + 2, snake[snake_idx].coordinates_y * GRID_SIZE + 2, GRID_SIZE - 4, GRID_SIZE - 4, 0xFFFFFF);
	}

	framebuffer_draw_rect(apple_x * GRID_SIZE + 5, apple_y * GRID_SIZE + 5, GRID_SIZE - 10, GRID_SIZE - 10, 0xFF0000);

	framebuffer_swap();
}

void snake_init()
{
	rng_seed = timer_get_ticks();
	if (rng_seed == 0) rng_seed = 12345;

	snake_reset();

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
		if (timer_get_ticks() - start_time >= 10)
		{
			start_time = timer_get_ticks();
			snake_update();
			snake_render();
		}
	}
}