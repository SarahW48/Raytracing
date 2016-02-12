
/* This is a ray cast function, which sent out a given ray p(t) = e + dt (the formula is in Shelly's graphics books)
 into the scene and returns the first object intersected by the ray and the time at which the intersection occurs.
 So the ray tracer could be implemented from the minimum time returned from the first object. 
 When the object became a airplane or some other more complicated models, there was no difference.*/

#include "raytracer.hpp"
#include "scene/scene.hpp"

#include <SDL/SDL_timer.h>
#include <iostream>


namespace _462 {

Raytracer::Raytracer()
    : scene( 0 ), width( 0 ), height( 0 ) { }

Raytracer::~Raytracer() { }

bool Raytracer::initialize( Scene* scene, size_t width, size_t height )
{
    this->scene = scene;
    this->width = width;
    this->height = height;

    current_row = 0;

    return true;
}

static bool refract(RayInfo ray, Vector3 normal, real_t refractiveindex, RayInfo &refractionray)
{
	real_t sign = 1-refractiveindex*refractiveindex*(1-dot(ray.direction,normal)*dot(ray.direction,normal));
	if(sign>0)
	{
		refractionray.direction = normalize(refractiveindex*(ray.direction-normal*dot(ray.direction,normal)) - normal*sqrt(sign));
		return true;
	}

	return false;

}

static Color3 raycolor(const Scene* scene, RayInfo ray, int n)
{
	Geometry* const* geometry = scene->get_geometries();
	const PointLight* light = scene->get_lights();
	IntersectionInfo intersection;
	intersection.t0 = EP;
	intersection.t1 = 1000000;
	int index;
	for(int i=0; i<scene->num_geometries();i++)
	{
		Matrix4 inversematrix;
		make_inverse_transformation_matrix(&inversematrix, geometry[i]->position, geometry[i]->orientation, geometry[i]->scale );
		RayInfo rayinformation;
		rayinformation.origin = inversematrix.transform_point(ray.origin);
		rayinformation.direction = inversematrix.transform_vector(ray.direction);	
		bool check = geometry[i]->check_geometry(rayinformation,intersection);
		
		if(check)
		{
			index = i;
		}
	}

	Color3 color = Color3::Black;
	if(intersection.t1 != 1000000)
	{
			Matrix4 transmatrix;
		    make_transformation_matrix(&transmatrix, geometry[index]->position, geometry[index]->orientation, geometry[index]->scale );
		    intersection.worldposition = transmatrix.transform_point(intersection.localposition);
			Matrix3 normalmatrix;
			make_normal_matrix(&normalmatrix, transmatrix );
			intersection.worldnormal = normalize(normalmatrix*intersection.localnormal);
			color = intersection.material.ambient*scene->ambient_light;

			for(int i=0; i<scene->num_lights();i++)
			{   
				RayInfo shadowworldrayinfo;
				shadowworldrayinfo.origin = intersection.worldposition;
				shadowworldrayinfo.direction = normalize(light[i].position - intersection.worldposition);
				bool hit = false;
				IntersectionInfo shadowintersection;
				shadowintersection.t0 = EP;
				shadowintersection.t1 = length(light[i].position - intersection.worldposition);
				real_t d = dot(intersection.worldnormal,shadowworldrayinfo.direction);
				if(d > 0)
				{
					for(int j=0; j<scene->num_geometries();j++)
					{
						Matrix4 shadowinversematrix;
						make_inverse_transformation_matrix(&shadowinversematrix, geometry[j]->position, geometry[j]->orientation, geometry[j]->scale );
						RayInfo shadowlocalrayinfo;
						shadowlocalrayinfo.origin = shadowinversematrix.transform_point(shadowworldrayinfo.origin);
						shadowlocalrayinfo.direction = shadowinversematrix.transform_vector(shadowworldrayinfo.direction);	
						bool check = geometry[j]->check_geometry(shadowlocalrayinfo,shadowintersection);
						if(check == true)
						{
								hit = true;
								break;
						}
					 }
					if(hit == false)
					{
						color = color + intersection.material.diffuse*light[i].color*d;
					}
				}
			}
			RayInfo reflectionworldrayinfo;
			reflectionworldrayinfo.origin = intersection.worldposition;
			Vector3 r = ray.direction - 2*dot(ray.direction,intersection.worldnormal)*intersection.worldnormal;
			reflectionworldrayinfo.direction = normalize(r);
			n++;
			
			if(intersection.material.refractive_index != 0)
			{
				real_t judge = dot(ray.direction,intersection.worldnormal);
				real_t c;
				RayInfo refractionworldrayinfo;
				refractionworldrayinfo.origin = intersection.worldposition;
				real_t refractiveratio = scene->refractive_index/intersection.material.refractive_index;

				if(judge<0)
				{
					
					refract(ray, intersection.worldnormal, refractiveratio,refractionworldrayinfo);
					c = -dot(ray.direction,intersection.worldnormal);
				}
				else
				{
					if(refract(ray, -intersection.worldnormal, 1/refractiveratio,refractionworldrayinfo))
					{
						c = dot(refractionworldrayinfo.direction,intersection.worldnormal);
					}
					else
					{
						if(n<=MAXNUMBER)
						{
							return intersection.material.specular*raycolor(scene,reflectionworldrayinfo,n);
						}
					}
				}
				  real_t R0 = (intersection.material.refractive_index-1)*(intersection.material.refractive_index-1)/(intersection.material.refractive_index+1)/(intersection.material.refractive_index+1);
			      real_t R = R0 + (1-R0)*pow(1-c,5);
				  if(n<=MAXNUMBER)
				  {
					return intersection.material.specular*(R*raycolor(scene,reflectionworldrayinfo,n) + (1-R)*raycolor(scene,refractionworldrayinfo,n));
				  }
			}
			else
			{
				if(n<=MAXNUMBER)
				{
					color = color + intersection.material.specular*raycolor(scene,reflectionworldrayinfo,n);
				}
			}
			return color;
			
	}

	return scene->background_color;

}


 // Performs a raytrace on the current scene
static Color3 trace_pixel( const Scene* scene, size_t x, size_t y, size_t width, size_t height )
{
    assert( 0 <= x && x < width );
    assert( 0 <= y && y < height );

	Vector3 camposition = scene->camera.get_position();
	Vector3 camdirection = scene ->camera.get_direction();
	Vector3 camup = scene ->camera.get_up();
	real_t camdegree = scene ->camera.get_fov_radians();
	real_t camratio = scene->camera.get_aspect_ratio();
	real_t camdistance = scene ->camera.get_near_clip();

	Vector3 camright = normalize(cross(camdirection, camup));
	real_t pixelheight = camdistance*tan(camdegree/2)/(height/2);
	real_t pixelwidth = camdistance*tan(camdegree/2)*camratio/(width/2);

    real_t x0 = (real_t)x-width/2+0.5;
	real_t y0 = (real_t)y-height/2+0.5;
	Vector3 raydirection = normalize(camdistance*camdirection + y0*pixelheight*camup + x0*pixelwidth*camright);
	RayInfo eyeray;
	eyeray.origin = camposition;
	eyeray.direction = raydirection;
	return raycolor(scene, eyeray, 0);
}

//  Raytraces some portion of the scene
bool Raytracer::raytrace( unsigned char *buffer, real_t* max_time )
{
    static const size_t PRINT_INTERVAL = 64;

    unsigned int end_time = 0;
    bool is_done;

    if ( max_time ) {
        // convert duration to milliseconds
        unsigned int duration = (unsigned int) ( *max_time * 1000 );
        end_time = SDL_GetTicks() + duration;
    }

    // until time is up, run the raytrace. we render an entire row at once
    for ( ; !max_time || end_time > SDL_GetTicks(); ++current_row ) {

        if ( current_row % PRINT_INTERVAL == 0 ) {
            printf( "Raytracing (row %u)...\n", current_row );
        }

        // we're done if we finish the last row
        is_done = current_row == height;
        if ( is_done )
            break;

        for ( size_t x = 0; x < width; ++x ) {
            // trace a pixel
            Color3 color = trace_pixel( scene, x, current_row, width, height );
            color.to_array( &buffer[4 * ( current_row * width + x )] );
        }
    }

    if ( is_done ) {
        printf( "Done raytracing!\n" );
    }

    return is_done;
}

} 

