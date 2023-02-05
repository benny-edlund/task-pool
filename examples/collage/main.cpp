/**
 * @file main.cpp
 * @author Benny Edlund (benny.edlund@gmail.com)
 * @brief
 * @date 2023-02-05
 *
 * @copyright Copyright (c) 2023
 *
 * @details
 * First off dont write this program, its a bad one. Im intentially going with a poor design to
 * show retrying of tasks.
 *
 * The goal of this program is build an image collage of a configurable density from Wikipedia
 * random articles.
 *
 * The program solves this by using curl to query the random/summary article. It then locates the
 * article image, decompresses it and inserts it into the final image after resizing it. The final
 * result is contigously updated.
 *
 * The trouble is that the program can only deal with jpeg images and not all article summaries use
 * jpegs as their image. Further it seems that Wikipedia routinely renames gifs and pngs to jpg so
 * its quite likley that the jpeg decompression fails. Experiments show about a 5% success rate.
 *
 * This deficiancy in the program was not resolved by adding support for other image format. Instead
 * we brute foce and just re-runs the task. Poor wikipedia.
 *
 * Needless to say you should not do this...but it does let us deal with a lot of errors.
 */
#include <CLI/CLI.hpp>
#include <algorithm>
#include <cassert>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <curses.h>
#include <exception>
#include <fmt/core.h>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <task_pool/pipes.h>
#include <task_pool/pool.h>
#include <thread>
#include <turbojpeg.h>

using json = nlohmann::json;

static std::atomic_size_t s_total_queries{ 0 };
static std::atomic_size_t s_success_queries{ 0 };

struct task_failure : public std::runtime_error
{
    explicit task_failure( std::string msg )
        : std::runtime_error( msg )
    {
    }
    ~task_failure() = default;
};

/**
 * @brief Rectangular dimentions
 *
 */
struct dimentions
{
    std::size_t width;
    std::size_t height;
    std::size_t size() const noexcept { return width * height; }
};

/**
 * @brief Basic pixel representation
 *
 * @tparam T
 */
template< typename T >
struct pixel_t
{
    using value_type = T;
    value_type red;
    value_type green;
    value_type blue;
};
using pixel = pixel_t< std::uint8_t >;
/**
 * @brief Basic image class
 *
 */
class Image
{
    dimentions           dims_;
    std::vector< pixel > pixels_;

public:
    Image()
        : dims_{ 0, 0 }
    {
    }
    explicit Image( dimentions dims ) // NOLINT
        : dims_{ dims }
        , pixels_( dims_.size(), pixel{ 0, 0, 0 } )
    {
    }
    ~Image()              = default;
    Image( Image const& ) = delete;
    Image( Image&& other ) noexcept
        : dims_( other.dims_ )
        , pixels_( std::move( other.pixels_ ) )
    {
        other.dims_ = {};
    }
    Image& operator=( Image const& ) = delete;
    Image& operator=( Image&& other ) noexcept
    {
        dims_       = other.dims_;
        other.dims_ = {};
        pixels_.clear();
        std::swap( pixels_, other.pixels_ );
        return *this;
    }
    dimentions            dims() const noexcept { return dims_; }
    std::vector< pixel >& pixels() noexcept { return pixels_; }
    explicit              operator bool() const noexcept { return pixels_.size() == dims_.size(); }
};

/**
 * @brief Compress and image to jpeg data
 *
 * @param img
 * @return auto
 */
auto compress( Image& img ) // NOLINT
{
    if ( img.dims().size() != 0 )
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
        auto ec = tjCompress2( jpegCompressor,
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
        if ( ec != 0 )
        {
            fmt::print( "[ {} ]:: Failed to compress image\n", __LINE__ );
            throw task_failure{ tjGetErrorStr() };
        }
        return std::make_pair( compressedImage, jpegSize );
    }
    unsigned char* none = nullptr;
    return std::make_pair( none, 0UL );
}

/**
 * @brief Decompress some jpeg data
 *
 * @param data
 * @return Image
 */
Image decompress( std::vector< std::uint8_t > data )
{
    int width     = 0;
    int height    = 0;
    int subsample = 0;

    tjhandle jpegDecompressor = tjInitDecompress();
    if ( 0 != tjDecompressHeader2( jpegDecompressor,
                                   data.data(),
                                   data.size() * sizeof( std::uint8_t ),
                                   &width,
                                   &height,
                                   &subsample ) )
    {
        tjDestroy( jpegDecompressor );
        throw task_failure( fmt::format(
            "[ {} ]: Failed to decompress jpeg header  ( {} )", __LINE__, tjGetErrorStr() ) );
    }
    Image img(
        dimentions{ static_cast< std::size_t >( width ), static_cast< std::size_t >( height ) } );
    if ( 0 != tjDecompress2( jpegDecompressor,
                             data.data(),
                             data.size(),
                             &img.pixels().data()->red,
                             width,
                             width * 3,
                             height,
                             TJPF_RGB,
                             0 ) )
    {
        tjDestroy( jpegDecompressor );
        throw task_failure( fmt::format( "[ {} ]: Failed to decompress jpeg body  ( {} )",
                                         __LINE__,
                                         tjGetErrorStr2( jpegDecompressor ) ) );
    }
    tjDestroy( jpegDecompressor );
    s_success_queries++;
    return img;
}

/**
 * @brief Write image to disk
 *
 * @param data
 * @param filename
 * @return unsigned*
 */
unsigned char* write( std::pair< unsigned char*, long unsigned int > data,
                      std::string const&                             filename )
{
    if ( data.first != nullptr )
    {
        std::ofstream writer;
        writer.open( filename, std::ios::out | std::ios::binary );
        // NOLINTNEXTLINE
        writer.write( reinterpret_cast< char* >( data.first ), std::streamsize( data.second ) );
    }
    return data.first;
}

/**
 * @brief curl data writer
 *
 * @param contents
 * @param size
 * @param nmemb
 * @param userp
 * @return size_t
 */
static size_t curl_write_function_( void* contents, size_t size, size_t nmemb, void* userp )
{
    static_cast< std::string* >( userp )->append( static_cast< char* >( contents ), size * nmemb );
    return size * nmemb;
}

/**
 * @brief curl -X /GET (in a thread)
 *
 * @param url
 * @return std::vector< std::uint8_t >
 */
std::vector< std::uint8_t > curl( std::string url )
{
    assert( !url.empty() );
    auto* curl = curl_easy_init();
    if ( curl != nullptr )
    {
        s_total_queries++;
        curl_easy_setopt( curl, CURLOPT_URL, url.c_str() );
        curl_easy_setopt( curl, CURLOPT_TCP_KEEPALIVE, 1L );
        curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );

        std::string response;
        std::string header;
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, curl_write_function_ );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );
        curl_easy_setopt( curl, CURLOPT_HEADERDATA, &header );

        curl_easy_perform( curl );
        curl_easy_cleanup( curl );
        return { response.begin(), response.end() };
    }
    throw task_failure( fmt::format( "[ {} ]: Failed to initialize curl", __LINE__ ) );
}

/**
 * @brief Find a wikipedia summary page that claims to use a jpeg as its image
 *
 * @param abort
 * @return std::string
 */
std::string find_random_jpeg( be::stop_token abort )
{
    while ( !abort )
    {
        json data =
            json::parse( curl( "https://en.wikipedia.org/api/rest_v1/page/random/summary" ) );
        if ( !data["originalimage"]["source"].empty() )
        {
            std::string output = data["originalimage"]["source"];
            if ( output.find( ".jpg" ) != output.npos )
            {
                return output;
            }
        }
    }
    return {};
}

/**
 * @brief Resize the input image to the given dimentions
 *
 * @param size
 * @param input
 * @param abort
 * @return Image
 */
Image resize_image( dimentions size, Image input, be::stop_token abort )
{
    try
    {
        Image output( size );
        float scale_width =
            static_cast< float >( input.dims().width ) / static_cast< float >( size.width );
        float scale_height =
            static_cast< float >( input.dims().height ) / static_cast< float >( size.height );
        std::size_t w = 0;
        std::size_t h = 0;
        while ( !abort )
        {
            auto        output_index = w + h * output.dims().width;
            std::size_t input_index =
                static_cast< std::size_t >( static_cast< float >( w ) * scale_width ) +
                ( input.dims().width *
                  static_cast< std::size_t >( static_cast< float >( h ) * scale_height ) );
            output.pixels().at( output_index ) = input.pixels().at( input_index );
            if ( ++w >= output.dims().width )
            {
                ++h;
                w = 0;
            }
            if ( h >= output.dims().height )
            {
                break;
            }
        }
        return output;
    }
    catch ( std::exception const& e )
    {
        throw task_failure(
            fmt::format( "[ {} ]: Failed to resize image ( {} )\n", __LINE__, e.what() ) );
    }
}

/**
 * @brief view-like image wrapper used to represent a unique tile in the output
 *
 */
struct ImageSection
{
    Image&     owner;
    dimentions start;
};

/**
 * @brief Write the input image into a section of the output
 *
 * @param output
 * @param input
 * @param abort
 */
void blit_image( ImageSection output, Image input, be::stop_token abort )
{
    std::size_t w = 0;
    std::size_t h = 0;
    try
    {
        while ( !abort )
        {
            std::size_t index = ( output.start.width + w ) +
                                output.owner.dims().width * ( output.start.height + h );
            output.owner.pixels().at( index ) = input.pixels().at( w + input.dims().width * h );
            if ( ++w >= input.dims().width )
            {
                ++h;
                w = 0;
            }
            if ( h >= input.dims().height )
            {
                break;
            }
        }
    }
    catch ( std::exception const& e )
    {
        throw task_failure(
            fmt::format( "[ {} ]: Failed to insert image section ( {} )\n", __LINE__, e.what() ) );
    }
}
/**
 * @brief Curl helper managing the global state
 *
 */
struct curl_context
{
    curl_context() { curl_global_init( CURL_GLOBAL_ALL ); }
    ~curl_context() { curl_global_cleanup(); }
};

/**
 * @brief ncurses helper managing the global window state
 *
 */
struct curses_contex
{
    curses_contex()
    {
        setlocale( LC_ALL, "" ); // NOLINT (not using return value)
        initscr();
    }
    ~curses_contex() { endwin(); }
};

int main( int argc, char** argv ) // NOLINT
{
    CLI::App    app;
    std::string filename;
    std::size_t tiles_width  = 10;
    std::size_t tiles_height = 10;
    app.description( "Create a collage of random images X tiles wide and Y tiles high." );
    app.add_option( "-x", tiles_width, "Amount of tiles wide (default:10)" );
    app.add_option( "-y", tiles_height, "Amount of tiles high (default:10)" );
    app.add_option( "filename", filename, "Output filename" )->required();
    CLI11_PARSE( app, argc, argv );

    if ( tiles_height > 0 && tiles_width > 0 )
    {
        curl_context  curl_ctx;
        curses_contex curses_ctx;
        be::task_pool pool;

        static const dimentions s_tile_size{ 128, 128 };
        static const dimentions s_img_size{ s_tile_size.width * tiles_width,
                                            s_tile_size.height * tiles_height };

        Image output( s_img_size );

        // we wrap the resize method so we can capture the tile size
        auto resize = []( Image img, be::stop_token abort ) {
            return resize_image( s_tile_size, std::move( img ), std::move( abort ) );
        };
        // main task pipeline, it ends up returning a future so we can monitor them
        auto fill_tile = [&]( std::size_t tile ) -> std::future< void > {
            auto       h = tile / tiles_width;
            auto       w = tile - h * tiles_width;
            dimentions start{ w * s_tile_size.width, h * s_tile_size.height };
            // clang-format off
            auto pipe = pool | &find_random_jpeg 
                             | &curl 
                             | &decompress 
                             | resize;
            return pool.submit( &blit_image, ImageSection{ output, start }, std::move(pipe) );
            // clang-format on
        };
        // our main compression and write function.
        auto write_file = [&]() -> std::future< void > {
            auto jpeg_data = pool.submit( &compress, std::ref( output ) );
            auto jpeg_ptr  = pool.submit( &write, std::move( jpeg_data ), std::ref( filename ) );
            return pool.submit( &tjFree, std::move( jpeg_ptr ) );
        };
        // basic console ui showing our progress
        auto render_ui = [&]( std::vector< bool > const& status ) {
            clear();
            refresh();
            int row = 0;
            move( ++row, 0 );
            printw(
                "Building image collage from random Wikipedia articles contigously writing to '%s'",
                filename.c_str() );
            move( ++row, 0 );
            printw( "(ctrl-c to stop)" );
            ++row;
            move( ++row, 0 );
            auto total   = s_total_queries.load();
            auto success = s_success_queries.load();
            printw( "total queries: %lu", total );
            move( ++row, 0 );
            printw(
                "success rate:  %d%%",
                static_cast< int >(
                    ( static_cast< float >( success ) / static_cast< float >( total ) ) * 100.F ) );
            move( ++row, 0 );
            printw( "threads:       %d", pool.get_thread_count() );
            move( ++row, 0 );
            printw( "tasks total:   %lu", pool.get_tasks_total() );
            move( ++row, 0 );
            printw( "tasks waiting: %lu", pool.get_tasks_waiting() );
            move( ++row, 0 );
            printw( "tasks queued:  %lu", pool.get_tasks_queued() );
            move( ++row, 0 );
            printw( "tasks running: %lu", pool.get_tasks_running() );
            ++row;
            ++row;
            static const int width = static_cast< int >( tiles_width * 2 );
            int              w     = 0;
            int              h     = 0;
            for ( bool state : status )
            {
                move( h + row, w );
                printw( state ? "\u2593\u2593" : "\u2591\u2591" ); // NOLINT;
                w += 2;
                if ( w == width )
                {
                    ++h;
                    w = 0;
                }
            }
            move( ++row + h, 0 );

            refresh();
        };

        // First we start writing the initial image...its black
        write_file().wait();

        // If we know what tile we are working on we can safely generate a view into the output
        // image that is unique to each task so we can write concurrently to it without locks.
        // As tiles are completed we will reduce this vector. This is done in the main thread only
        // so will not need any locks.
        using section_vector = std::vector< std::pair< std::size_t, std::future< void > > >;
        section_vector sections( tiles_width * tiles_height );

        // Next we launch the inital tasks build the output images
        std::size_t section = 0;
        std::generate( sections.begin(), sections.end(), [&]() {
            std::size_t tile_count = section++;
            return std::make_pair( tile_count, fill_tile( tile_count ) );
        } );
        // We also generate a parallel status vector for the ui
        std::vector< bool > status( tiles_width * tiles_height );
        std::fill( status.begin(), status.end(), false );

        // until we are done
        for ( ;; )
        {
            using namespace std::chrono_literals;
            if ( sections.empty() )
            {
                break; // we are done
            }
            // This is a efficient way to move all the jobs that are completed to the end of
            // the vector.
            auto new_end =
                std::partition( sections.begin(), sections.end(), []( auto const& tile ) {
                    return tile.second.wait_for( 0s ) != std::future_status::ready;
                } );
            std::vector< std::size_t > relaunch;
            bool                       any_success = false;
            // Check the result of all tasks that have completed. Since we use exceptions a failure
            // is a thrown exception. If its a 'task_failure' we will requeue the tile and try
            // another random article
            std::for_each( new_end, sections.end(), [&]( auto& tile ) {
                try
                {
                    tile.second.get();
                    any_success        = true;
                    status[tile.first] = true;
                }
                catch ( task_failure const& )
                {
                    relaunch.push_back( tile.first );
                }
                catch ( std::exception const& e )
                {
                    fmt::print( e.what() );
                    std::terminate(); // we have a bug
                }
            } );
            // remove successful sections
            sections.erase( new_end, sections.end() );
            // relaunch failed sections
            for ( auto tile : relaunch )
            {
                sections.emplace_back( tile, fill_tile( tile ) );
            }
            // if any jobs succeeded this iteration then update the image
            if ( any_success )
            {
                write_file();
            }
            // well yeah...render the ui
            render_ui( status );
            // We are IO bound on the internet queries so no need to busy check this but still we
            // want to show we are responsive in the ui
            std::this_thread::sleep_for( 120ms );
        }
        auto done = write_file();
        try
        {
            done.get();
        }
        catch ( std::exception const& e )
        {
            fmt::print( "Failed to write image [{}]\n", e.what() );
        }
    }
    return 0;
}
