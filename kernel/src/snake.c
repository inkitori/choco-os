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

static uint64_t apple_x = 1;
static uint64_t apple_y = 1;

static bool just_ate_apple = false;
static bool kill_sig = false;

uint64_t tail_x;
uint64_t tail_y;

static void snake_update()
{
	if (just_ate_apple)
	{
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
	if (key == A)
		direction = LEFT;
	else if (key == D)
		direction = RIGHT;
	else if (key == W)
		direction = UP;
	else if (key == S)
		direction = DOWN;

	if (direction == LEFT)
		snake[0].coordinates_x--;
	else if (direction == RIGHT)
		snake[0].coordinates_x++;
	else if (direction == UP)
		snake[0].coordinates_y--;
	else if (direction == DOWN)
		snake[0].coordinates_y++;

	if (snake[0].coordinates_x == apple_x && snake[0].coordinates_y == apple_y)
	{
		apple_x += 1;
		apple_y += 1;

		just_ate_apple = true;

		tail_x = snake[snake_size - 1].coordinates_x;
		tail_y = snake[snake_size - 1].coordinates_y;
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
}

static void snake_reset()
{
	direction = DOWN;
	snake_size = 1;
	kill_sig = false;
	just_ate_apple = false;

	apple_x = 1;
	apple_y = 1;
}

void snake_init()
{
	snake_reset();

	snake[0].coordinates_x = 0;
	snake[0].coordinates_y = 0;

	framebuffer_clear(0x000000);

	uint64_t start_time = timer_get_ticks();

	while (1)
	{
		if (kill_sig)
			return;
		if (timer_get_ticks() - start_time >= 10)
		{
			start_time = timer_get_ticks();
			snake_update();
			snake_render();
		}
	}
}
