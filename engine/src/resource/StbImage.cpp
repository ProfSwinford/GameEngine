#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG

#define STBI_NO_STDIO

#define STB_IMAGE_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif