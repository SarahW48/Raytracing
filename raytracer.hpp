
#ifndef _462_RAYTRACER_HPP_
#define _462_RAYTRACER_HPP_
#define EP 0.00001
#define MAXNUMBER 3

#include "math/color.hpp"

namespace _462 {

class Scene;

class Raytracer
{
public:

    Raytracer();

    ~Raytracer();

    bool initialize( Scene* scene, size_t width, size_t height );

    bool raytrace( unsigned char* buffer, real_t* max_time );

private:

    // the scene to trace
    Scene* scene;

    // the dimensions of the image to trace
    size_t width, height;

    // the next row to raytrace
    size_t current_row;

};

} 

#endif 

