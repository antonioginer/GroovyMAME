/**************************************************************

   grid.cpp - Simple test grid

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h> 
	
int main(int argc, char **argv)
{ 
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{ 
		printf("error initializing SDL: %s\n", SDL_GetError());
		return 1;
	}

	// Get display index
	int display_index = 0;
	if (argc > 1)
	{
		sscanf(argv[1], "%d", &display_index);

		int num_displays = SDL_GetNumVideoDisplays();
		if (display_index < 0 || display_index > num_displays - 1)
		{
			printf("error, bad display_index: %d\n", display_index);
			return 1;
		}
	}
	
	// Get target display size
	SDL_DisplayMode dm;
	SDL_GetCurrentDisplayMode(display_index, &dm);
	int width = dm.w;
	int height = dm.h;

	// Create window
	SDL_Window* win = SDL_CreateWindow("Switchres test", SDL_WINDOWPOS_CENTERED_DISPLAY(display_index), SDL_WINDOWPOS_CENTERED, width, height, 0);
	SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);

	// Create renderer
	SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	SDL_RenderClear(renderer);

	// Draw outer rectangle
	SDL_Rect rect {0, 0, width, height};
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	SDL_RenderDrawRect(renderer, &rect);

	// Draw grid
	for (int i = 0;  i < width / 16; i++)
	{
		for (int j = 0; j < height / 16; j++)
		{
			if (i == 0 || j == 0 || i == (width / 16) - 1 || j == (height / 16) - 1)
				SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
			else
				SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

			rect = {i * 16, j * 16, 16, 16};
			SDL_RenderDrawRect(renderer, &rect);

			rect = {i * 16 + 7, j * 16 + 7, 2, 2};
			SDL_RenderDrawRect(renderer, &rect);
		}
	}

	SDL_RenderPresent(renderer);

	// Wait for escape key
	bool close = false;
	while (!close)
	{ 
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{ 
			switch (event.type)
			{ 
				case SDL_QUIT:
					close = true;
					break; 

				case SDL_KEYDOWN: 
					switch (event.key.keysym.scancode)
					{
						case SDL_SCANCODE_ESCAPE:
							close = true;
							break; 

						default:
							break;
					}
			}
		}
	}

	SDL_DestroyWindow(win);
	SDL_Quit();

	return 0; 
} 
