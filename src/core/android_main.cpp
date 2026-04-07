#include <SDL3/SDL_main.h>

extern "C" int app_main();

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    return app_main();
}
