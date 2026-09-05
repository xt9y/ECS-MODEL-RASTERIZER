#include <cbuild.h>

static void configurePlatform(C_Target *target)
{
#ifdef __APPLE__
    c_define(target, "GL_SILENCE_DEPRECATION");
    c_include(target, "/opt/homebrew/include");
    c_include(target, "/usr/local/include");
    c_link_flag(target, "-L/opt/homebrew/lib");
    c_link_flag(target, "-L/usr/local/lib");
    c_link_system(target, "glfw");
    c_framework(target, "OpenGL");
    c_framework(target, "Cocoa");
    c_framework(target, "IOKit");
    c_framework(target, "CoreVideo");
#else
    c_link_system(target, "glfw");
    c_link_system(target, "GL");
    c_link_system(target, "GLU");
    c_link_system(target, "m");
    c_link_system(target, "dl");
    c_link_system(target, "pthread");
#endif
}

static void configureApp(C_Target *target)
{
    c_warnings_strict(target);
    c_include(target, ".");
    c_include(target, "Sources");
    c_include(target, "/usr/local/include");
    c_link_system(target, "lwcgl");
    configurePlatform(target);
}

void build(C_Build *b)
{
    C_Target *app = c_executable(b, "rw-engine");

    c_sources(app, "main.cpp");
    c_sources(app, "Sources/*.cpp");
    c_sources(app, "Sources/*/*.cpp");
    c_sources(app, "Sources/*/*/*.cpp");

    c_flag(app, "-std=c++17");
    configureApp(app);
    c_link_system(app, "stdc++");
    c_default_target(b, app);
}
