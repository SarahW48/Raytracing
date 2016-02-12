
#include "application/application.hpp"
#include "application/camera_roam.hpp"
#include "application/imageio.hpp"
#include "application/scene_loader.hpp"
#include "application/opengl.hpp"
#include "scene/scene.hpp"
#include "raytracer/raytracer.hpp"

#include <iostream>
#include <cstring>

namespace _462 {

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

#define BUFFER_SIZE(w,h) ( (size_t) ( 4 * (w) * (h) ) )

#define KEY_RAYTRACE SDLK_r
#define KEY_SCREENSHOT SDLK_f

static const GLenum LightConstants[] = {
    GL_LIGHT0, GL_LIGHT1, GL_LIGHT2, GL_LIGHT3,
    GL_LIGHT4, GL_LIGHT5, GL_LIGHT6, GL_LIGHT7
};
static const size_t NUM_GL_LIGHTS = 8;

// renders a scene using opengl
static void render_scene( const Scene& scene );

struct Options
{
    bool open_window;
    const char* input_filename;
    const char* output_filename;
    int width, height;
};

class RaytracerApplication : public Application
{
public:

    RaytracerApplication( const Options& opt )
        : options( opt ), buffer( 0 ), buf_width( 0 ), buf_height( 0 ), raytracing( false ) { }
    virtual ~RaytracerApplication() { free( buffer ); }

    virtual bool initialize();
    virtual void destroy();
    virtual void update( real_t );
    virtual void render();
    virtual void handle_event( const SDL_Event& event );

    // flips raytracing, does any necessary initialization
    void toggle_raytracing( int width, int height );
    // writes the current raytrace buffer to the output file
    void output_image();

    Raytracer raytracer;

    Scene scene;

    Options options;

    CameraRoamControl camera_control;

    unsigned char* buffer;
    int buf_width, buf_height;
    bool raytracing;
    bool raytrace_finished;
};

bool RaytracerApplication::initialize()
{
    camera_control.camera = scene.camera;
    bool load_gl = options.open_window;

    try {

        Material* const* materials = scene.get_materials();
        Mesh* const* meshes = scene.get_meshes();

        // load all textures
        for ( size_t i = 0; i < scene.num_materials(); ++i ) {
            if ( !materials[i]->load() || ( load_gl && !materials[i]->create_gl_data() ) ) {
                std::cout << "Error loading texture, aborting.\n";
                return false;
            }
        }

        // load all meshes
        for ( size_t i = 0; i < scene.num_meshes(); ++i ) {
            if ( !meshes[i]->load() || ( load_gl && !meshes[i]->create_gl_data() ) ) {
                std::cout << "Error loading mesh, aborting.\n";
                return false;
            }
        }

    } catch ( std::bad_alloc const& ) {
        std::cout << "Out of memory error while initializing scene\n.";
        return false;
    }

    // set the gl state
    if ( load_gl ) {
        float arr[4];
        arr[3] = 1.0; 

        glClearColor(
            scene.background_color.r,
            scene.background_color.g,
            scene.background_color.b,
            1.0f );

        scene.ambient_light.to_array( arr );
        glLightModelfv( GL_LIGHT_MODEL_AMBIENT, arr );

        const PointLight* lights = scene.get_lights();

        for ( size_t i = 0; i < NUM_GL_LIGHTS && i < scene.num_lights(); i++ ) {
            const PointLight& light = lights[i];
            glEnable( LightConstants[i] );
            light.color.to_array( arr );
            glLightfv( LightConstants[i], GL_DIFFUSE, arr );
            glLightfv( LightConstants[i], GL_SPECULAR, arr );
            glLightf( LightConstants[i], GL_CONSTANT_ATTENUATION, light.attenuation.constant );
            glLightf( LightConstants[i], GL_LINEAR_ATTENUATION, light.attenuation.linear );
            glLightf( LightConstants[i], GL_QUADRATIC_ATTENUATION, light.attenuation.quadratic );
        }

        glLightModeli( GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE );
    }

    return true;
}

void RaytracerApplication::destroy()
{

}

void RaytracerApplication::update( real_t delta_time )
{
    if ( raytracing ) {
        // do part of the raytrace
        if ( !raytrace_finished ) {
            assert( buffer );
            raytrace_finished = raytracer.raytrace( buffer, &delta_time );
        }
    } else {
        // copy camera over from camera control (if not raytracing)
        camera_control.update( delta_time );
        scene.camera = camera_control.camera;
    }
}

void RaytracerApplication::render()
{
    int width, height;

    get_dimension( &width, &height );
    glViewport( 0, 0, width, height );

    Camera& camera = scene.camera;
    camera.aspect = real_t( width ) / real_t( height );

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    if ( raytracing ) {
        assert( buffer );
        glColor4d( 1.0, 1.0, 1.0, 1.0 );
        glRasterPos2f( -1.0f, -1.0f );
        glDrawPixels( buf_width, buf_height, GL_RGBA, GL_UNSIGNED_BYTE, &buffer[0] );

    } else {
        glPushAttrib( GL_ALL_ATTRIB_BITS );
        render_scene( scene );
        glPopAttrib();
    }
}

void RaytracerApplication::handle_event( const SDL_Event& event )
{
    int width, height;

    if ( !raytracing ) {
        camera_control.handle_event( this, event );
    }

    switch ( event.type )
    {
    case SDL_KEYDOWN:
        switch ( event.key.keysym.sym )
        {
        case KEY_RAYTRACE:
            get_dimension( &width, &height );
            toggle_raytracing( width, height );
            break;
        case KEY_SCREENSHOT:
            output_image();
            break;
        default:
            break;
        }
    default:
        break;
    }
}

void RaytracerApplication::toggle_raytracing( int width, int height )
{
    assert( width > 0 && height > 0 );

    if ( !raytracing ) {

        if ( buf_width != width || buf_height != height ) {
            free( buffer );
            buffer = (unsigned char*) malloc( BUFFER_SIZE( width, height ) );
            if ( !buffer ) {
                std::cout << "Unable to allocate buffer.\n";
                return; 
            }
            buf_width = width;
            buf_height = height;
        }

        scene.camera.aspect = real_t( width ) / real_t( height );

        if ( !raytracer.initialize( &scene, width, height ) ) {
            std::cout << "Raytracer initialization failed.\n";
            return; 
        }

        raytrace_finished = false;
    }

    raytracing = !raytracing;
}

void RaytracerApplication::output_image()
{
    static const size_t MAX_LEN = 256;
    const char* filename;
    char buf[MAX_LEN];

    if ( !buffer ) {
        std::cout << "No image to output.\n";
        return;
    }

    assert( buf_width > 0 && buf_height > 0 );

    filename = options.output_filename;

    if ( !filename ) {
        imageio_gen_name( buf, MAX_LEN );
        filename = buf;
    }

    if ( imageio_save_image( filename, buffer, buf_width, buf_height ) ) {
        std::cout << "Saved raytraced image to '" << filename << "'.\n";
    } else {
        std::cout << "Error saving raytraced image to '" << filename << "'.\n";
    }
}


static void render_scene( const Scene& scene )
{
    // backup state so it doesn't mess up raytrace image rendering
    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glPushClientAttrib( GL_CLIENT_ALL_ATTRIB_BITS );

    glClearColor(
        scene.background_color.r,
        scene.background_color.g,
        scene.background_color.b,
        1.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glEnable( GL_NORMALIZE );
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_LIGHTING );
    glEnable( GL_TEXTURE_2D );

    // set camera transform
    const Camera& camera = scene.camera;

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    gluPerspective( camera.get_fov_degrees(),
                    camera.get_aspect_ratio(),
                    camera.get_near_clip(),
                    camera.get_far_clip() );

    const Vector3& campos = camera.get_position();
    const Vector3 camref = camera.get_direction() + campos;
    const Vector3& camup = camera.get_up();

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    gluLookAt( campos.x, campos.y, campos.z,
               camref.x, camref.y, camref.z,
               camup.x,  camup.y,  camup.z );
    // set light data
    float arr[4];
    arr[3] = 1.0; 
    scene.ambient_light.to_array( arr );
    glLightModelfv( GL_LIGHT_MODEL_AMBIENT, arr );

    glLightModeli( GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE );

    const PointLight* lights = scene.get_lights();

    for ( size_t i = 0; i < NUM_GL_LIGHTS && i < scene.num_lights(); i++ ) {
        const PointLight& light = lights[i];
        glEnable( LightConstants[i] );
        light.color.to_array( arr );
        glLightfv( LightConstants[i], GL_DIFFUSE, arr );
        glLightfv( LightConstants[i], GL_SPECULAR, arr );
        glLightf( LightConstants[i], GL_CONSTANT_ATTENUATION, light.attenuation.constant );
        glLightf( LightConstants[i], GL_LINEAR_ATTENUATION, light.attenuation.linear );
        glLightf( LightConstants[i], GL_QUADRATIC_ATTENUATION, light.attenuation.quadratic );
        light.position.to_array( arr );
        glLightfv( LightConstants[i], GL_POSITION, arr );
    }
    // render each object
    Geometry* const* geometries = scene.get_geometries();

    for ( size_t i = 0; i < scene.num_geometries(); ++i ) {
        const Geometry& geom = *geometries[i];
        Vector3 axis;
        real_t angle;

        glPushMatrix();

        glTranslated( geom.position.x, geom.position.y, geom.position.z );
        geom.orientation.to_axis_angle( &axis, &angle );
        glRotated( angle * ( 180.0 / PI ), axis.x, axis.y, axis.z );
        glScaled( geom.scale.x, geom.scale.y, geom.scale.z );

        geom.render();

        glPopMatrix();
    }

    glPopClientAttrib();
    glPopAttrib();
}

} 
static bool parse_args( Options* opt, int argc, char* argv[] )
{
    int input_index = 1;

    if ( argc < 2 ) {
        print_usage( argv[0] );
        return false;
    }

    if ( strcmp( argv[1], "-r" ) == 0 ) {
        opt->open_window = false;
        ++input_index;
    } else {
        opt->open_window = true;
    }

    if ( argc <= input_index ) {
        print_usage( argv[0] );
        return false;
    }

    if ( strcmp( argv[input_index], "-d" ) == 0 ) {
        if ( argc <= input_index + 3 ) {
            print_usage( argv[0] );
            return false;
        }

        // parse window dimensions
        opt->width = -1;
        opt->height = -1;
        sscanf( argv[input_index + 1], "%d", &opt->width );
        sscanf( argv[input_index + 2], "%d", &opt->height );
        // check for valid width/height
        if ( opt->width < 1 || opt->height < 1 ) {
            std::cout << "Invalid window dimensions\n";
            return false;
        }

        input_index += 3;
    } else {
        opt->width = DEFAULT_WIDTH;
        opt->height = DEFAULT_HEIGHT;
    }

    opt->input_filename = argv[input_index];

    if ( argc > input_index + 1 ) {
        opt->output_filename = argv[input_index + 1];
    } else {
        opt->output_filename = 0;
    }

    if ( argc > input_index + 2 ) {
        std::cout << "Too many arguments.\n";
        return false;
    }

    return true;
}

int main( int argc, char* argv[] )
{
    Options opt;

    Matrix3 mat;
    Matrix4 trn;
    make_transformation_matrix( &trn, Vector3::Zero, Quaternion::Identity, Vector3( 2, 2, 2 ) );

    make_normal_matrix( &mat, trn );

    if ( !parse_args( &opt, argc, argv ) ) {
        return 1;
    }

    RaytracerApplication app( opt );

    // load the given scene
    if ( !load_scene( &app.scene, opt.input_filename ) ) {
        std::cout << "Error loading scene " << opt.input_filename << ". Aborting.\n";
        return 1;
    }

    if ( opt.open_window ) {

        real_t fps = 30.0;
        const char* title = "15462 Project 2 - Raytracer";
        // start a new application
        return Application::start_application( &app, opt.width, opt.height, fps, title );

    } else {

        app.initialize();
        app.toggle_raytracing( opt.width, opt.height );
        if ( !app.raytracing ) {
            return 1; 
        }
        assert( app.buffer );
        // raytrace until done
        app.raytracer.raytrace( app.buffer, 0 );
        // output result
        app.output_image();
        return 0;

    }
}

