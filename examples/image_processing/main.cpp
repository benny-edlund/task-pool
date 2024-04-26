#include <CLI/CLI.hpp>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fmt/core.h>
#include <fstream>
#include <ios>
#include <memory>
#include <random>
#include <task_pool/pipes.h>
#include <task_pool/pool.h>
#include <turbojpeg.h>

static const std::uint8_t s_max = 255;

struct dimentions
{
    std::size_t width;
    std::size_t height;
    std::size_t size() const noexcept { return width * height; }
};

template< typename T >
struct pixel_t
{
    using value_type = T;
    value_type red;
    value_type green;
    value_type blue;
    value_type alpha;
};
using pixel = pixel_t< std::uint8_t >;

class Image
{
    dimentions           dims_;
    std::vector< pixel > pixels_;

public:
    Image() = delete;
    explicit Image( dimentions dims ) // NOLINT
        : dims_{ dims }
        , pixels_( dims_.size(), pixel{ 0, 0, 0, s_max } )
    {
    }
    dimentions            dims() const noexcept { return dims_; }
    std::vector< pixel >& pixels() noexcept { return pixels_; }
    explicit              operator bool() const noexcept { return pixels_.size() == dims_.size(); }
};

struct processor
{
    using ptr = std::unique_ptr< processor >;

    virtual ~processor()             = default;
    virtual Image run( Image ) const = 0;

    processor()                   = default;
    processor( processor const& ) = default;
    processor& operator=( processor const& ) = default;
    processor( processor&& )                 = default;
    processor& operator=( processor&& ) = default;
};

template< std::size_t Factor >
struct scaler : public processor
{
    Image run( Image img ) const override
    {
        fmt::print( "Scaler running on Image[{},{}]\n", img.dims().width, img.dims().height );
        float f = static_cast< float >( Factor ) / 100.F;
        if ( img )
        {
            auto out =
                Image( { static_cast< size_t >( static_cast< float >( img.dims().width ) * f ),
                         static_cast< size_t >( static_cast< float >( img.dims().height ) * f ) } );
            auto const inv_factor = 1 / f;
            float      w          = 0;
            float      h          = 0;
            for ( pixel& pix : out.pixels() )
            {
                std::size_t index =
                    static_cast< std::size_t >( w * inv_factor ) +
                    ( img.dims().width * static_cast< std::size_t >( h * inv_factor ) );
                assert( index < img.dims().size() ); // NOLINT
                pix = img.pixels()[index];
                if ( static_cast< std::size_t >( ++w ) == out.dims().width )
                {
                    w = 0;
                    ++h;
                }
            }
            fmt::print( "Scaler returning Image[{},{}]\n", out.dims().width, out.dims().height );
            return out;
        }
        return img;
    }
};

struct randomize : public processor
{
    Image run( Image img ) const override
    {
        fmt::print( "randomize running on Image[{},{}]\n", img.dims().width, img.dims().height );
        std::random_device                            rd{};
        std::mt19937                                  gen( rd() );
        std::uniform_int_distribution< std::uint8_t > distrib( 0, s_max );
        std::generate( img.pixels().begin(), img.pixels().end(), [&]() {
            return pixel{ distrib( gen ), distrib( gen ), distrib( gen ), s_max };
        } );
        return img;
    }
};

template< std::size_t X, std::size_t Y >
struct point
{
    static constexpr std::size_t x = X;
    static constexpr std::size_t y = Y;
};

template< typename Start, typename End >
struct crop : public processor
{
    Image run( Image img ) const override
    {
        fmt::print( "crop running on Image[{},{}]\n", img.dims().width, img.dims().height );
        Image out( dimentions{ End::x - Start::x, End::y - Start::y } );
        if ( Start::x < img.dims().width && Start::y < img.dims().height &&
             End::x < img.dims().width && End::y < img.dims().height )
        {
            std::size_t w = Start::x;
            std::size_t h = Start::y;
            for ( auto& pix : out.pixels() )
            {
                pix = img.pixels()[w + w * h];
                if ( ++w == End::x )
                {
                    w = Start::x;
                    ++h;
                }
            }
        }
        fmt::print( "crop returning Image[{},{}]\n", out.dims().width, out.dims().height );
        return out;
    }
};

template< typename... Task >
std::array< processor::ptr, sizeof...( Task ) > get_workload()
{
    return { std::unique_ptr< processor >( new Task() )... };
}

auto compress( Image img ) // NOLINT
{
    const int         JPEG_QUALITY    = 75;
    int               width           = static_cast< int >( img.dims().width );
    int               height          = static_cast< int >( img.dims().height );
    long unsigned int jpegSize        = 0;
    unsigned char*    compressedImage = nullptr;

    tjhandle                     jpegCompressor = tjInitCompress();
    std::vector< unsigned char > raw_image( img.dims().width * img.dims().height * 3 );
    std::size_t                  i = 0;
    for ( auto const& pxl : img.pixels() )
    {
        raw_image[i++] = pxl.red;
        raw_image[i++] = pxl.green;
        raw_image[i++] = pxl.blue;
    }
    tjCompress2( jpegCompressor,
                 raw_image.data(),
                 width,
                 0,
                 height,
                 TJPF_RGB,
                 &compressedImage,
                 &jpegSize,
                 TJSAMP_444,
                 JPEG_QUALITY,
                 TJFLAG_FASTDCT );

    tjDestroy( jpegCompressor );

    return std::make_pair( compressedImage, jpegSize );
}

unsigned char* write( std::pair< unsigned char*, long unsigned int > data,
                      std::string const&                             filename )
{
    std::ofstream writer;
    writer.open( filename, std::ios::out | std::ios::binary );
    // NOLINTNEXTLINE
    writer.write( reinterpret_cast< char* >( data.first ), std::streamsize( data.second ) );
    return data.first;
}

int main( int argc, char** argv ) // NOLINT
{
    CLI::App    app;
    std::string filename;
    app.add_option( "filename", filename, "Output filename" )->required();
    CLI11_PARSE( app, argc, argv );

    be::task_pool        pool;
    std::future< Image > result = pool.submit( std::launch::async, []() {
        return Image( dimentions{ s_max, s_max } );
    } );

    auto workload = get_workload< randomize,
                                  crop< point< 10, 10 >, point< 200, 200 > >, // NOLINT
                                  scaler< 50 > >();                           // NOLINT
    for ( auto const& work : workload )
    {
        result = pool.submit( std::launch::async, &processor::run, work.get(), std::move( result ) );
    }
    auto jpeg_data = pool.submit( std::launch::async, &compress, std::move( result ) );
    auto jpeg_ptr  = pool.submit( std::launch::async, &write, std::move( jpeg_data ), std::ref( filename ) );
    auto done      = pool.submit( std::launch::async, &tjFree, std::move( jpeg_ptr ) );
    try
    {
        done.get();
        fmt::print( "Result written to {}\n", filename );
        return 0;
    }
    catch ( std::exception const& e )
    {
        fmt::print( "Failed to write image [{}]", e.what() );
        return -1;
    }
}
