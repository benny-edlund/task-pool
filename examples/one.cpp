#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fmt/core.h>
#include <memory>
#include <random>
#include <task_pool/pipes.h>
#include <task_pool/pool.h>

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
    explicit Image( dimentions dims ) //NOLINT
        : dims_{ dims }
        , pixels_( dims_.size(), pixel{ 0, 0, 0, s_max } )
    {
    }
    dimentions            dims() const noexcept { return dims_; }
    std::vector< pixel >& data() noexcept { return pixels_; }
    explicit              operator bool() const noexcept { return pixels_.size() == dims_.size(); }
};

struct processor
{
    using ptr                        = std::unique_ptr< processor >;
    virtual ~processor()             = default;
    virtual Image run( Image ) const = 0;

    processor()                              = default;
    processor( processor const& )            = default;
    processor& operator=( processor const& ) = default;
    processor( processor&& )                 = default;
    processor& operator=( processor&& )      = default;
};

template< std::size_t Factor >
struct scaler : public processor
{
    Image run( Image img ) const override
    {
        fmt::print("Scaler running on Image[{},{}]\n",img.dims().width,img.dims().height);
        float f = static_cast< float >( Factor ) / 100.F;
        if ( img )
        {
            auto out =
                Image( { static_cast< size_t >( static_cast< float >( img.dims().width ) * f ),
                         static_cast< size_t >( static_cast< float >( img.dims().height ) * f ) } );
            auto const inv_factor = 1 / f;
            float      w          = 0;
            float      h          = 0;
            for ( pixel& pix : out.data() )
            {
                std::size_t index =
                    static_cast< std::size_t >( w * inv_factor ) +
                    ( img.dims().width * static_cast< std::size_t >( h * inv_factor ) );
                assert( index < img.dims().size() ); // NOLINT
                pix = img.data()[index];
                if ( static_cast< std::size_t >( ++w ) == out.dims().width )
                {
                    w = 0;
                    ++h;
                }
            }
            fmt::print("Scaler returning Image[{},{}]\n",out.dims().width,out.dims().height);
            return out;
        }
        return img;
    }
};

struct randomize : public processor
{
    Image run( Image img ) const override
    {
        fmt::print("randomize running on Image[{},{}]\n",img.dims().width,img.dims().height);
        std::random_device                            rd{};
        std::mt19937                                  gen( rd() );
        std::uniform_int_distribution< std::uint8_t > distrib( 0, s_max );
        std::generate( img.data().begin(), img.data().end(), [&]() {
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
        fmt::print("crop running on Image[{},{}]\n",img.dims().width,img.dims().height);
        Image out( dimentions{ End::x - Start::x, End::y - Start::y } );
        if ( Start::x < img.dims().width && Start::y < img.dims().height &&
             End::x < img.dims().width && End::y < img.dims().height )
        {
            std::size_t w = Start::x;
            std::size_t h = Start::y;
            for ( auto& pix : out.data() )
            {
                pix = img.data()[w + w * h];
                if ( ++w == End::x )
                {
                    w = Start::x;
                    ++h;
                }
            }
        }
        fmt::print("crop returning Image[{},{}]\n",out.dims().width,out.dims().height);
        return out;
    }
};

template< typename... Task >
std::array< processor::ptr, sizeof...( Task ) > get_workload()
{
    return { std::unique_ptr< processor >( new Task() )... };
}

int main() // NOLINT
{
    be::task_pool        pool;
    std::future< Image > result   = pool.submit( []() {
        return Image( dimentions{ s_max, s_max } );
    } );
    auto                 workload = get_workload< randomize,
                                  crop< point< 10, 10 >, point< 200, 200 > >, // NOLINT
                                  scaler< 50 > >(); // NOLINT
    for ( auto const& work : workload )
    {
        result = pool.submit( &processor::run, work.get(), std::move( result ) );
    }
    Image image = result.get(); 
    fmt::print("Result Image[{},{}]\n",image.dims().width,image.dims().height);
    return 0;
}