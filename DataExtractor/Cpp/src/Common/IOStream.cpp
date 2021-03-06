#include "IOStream.h"

#include <cstdio>
#include <cstdarg>

#include "ScoreDefinitions.h"

constexpr U32 SCORE_VERSION = 1;
constexpr U32 TIMELINES_PER_FILE = 100;
constexpr U32 TIMELINE_FILE_NUM_DIGITS = 4;

static_assert(TIMELINES_PER_FILE > 0);
static_assert(TIMELINE_FILE_NUM_DIGITS > 0);

namespace IO
{ 
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Logging
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct GlobalParams
    { 
        GlobalParams()
            : verbosity(Verbosity::Progress)
        {}

        Verbosity verbosity;
    };
    
    GlobalParams g_globals;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // -----------------------------------------------------------------------------------------------------------
    void SetVerbosityLevel(const Verbosity level)
    { 
        g_globals.verbosity = level;
    }

    // -----------------------------------------------------------------------------------------------------------
    void Log(const Verbosity level, const char* format,...)
    { 
        if (level <= g_globals.verbosity)
        { 
            va_list argptr;
            va_start(argptr, format);
            vfprintf(stderr, format, argptr);
            va_end(argptr);
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    void LogTime(const Verbosity level, const char* prefix, long miliseconds)
    { 
        long seconds = miliseconds/1000; 
        miliseconds  = miliseconds - (seconds*1000);

        long minutes = seconds/60; 
        seconds      = seconds - (minutes*60);

        long hours   = minutes/60; 
        minutes      = minutes - (hours*60);

             if (hours)   Log(level, "%s%02uh %02um",  prefix, hours,   minutes);
        else if (minutes) Log(level, "%s%02um %02us",  prefix, minutes, seconds);
        else if (seconds) Log(level, "%s%02us %02ums", prefix, seconds, miliseconds);
        else              Log(level, "%s%02ums",       prefix, miliseconds);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Input File
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    FileBuffer ReadFile(const char* filename)
    {
        FileBuffer content = nullptr; 

        FILE* stream;
        const errno_t result = fopen_s(&stream,filename,"rb");

        if (result) 
        { 
            LOG_ERROR("Unable to open input file!");
        }
        else 
        { 
            fseek(stream, 0, SEEK_END);
            long fsize = ftell(stream);
            fseek(stream, 0, SEEK_SET);  // same as rewind(f);
            
            content = new char[(fsize+1ull)];
            if (fread(content, 1, fsize, stream) == 0)
            { 
                LOG_ERROR("Something went wrong while reading the file %s.",filename);
                DestroyBuffer(content);
            }
            else 
            { 
                content[fsize] = '\0';
            }
        }
        
        fclose(stream);
        
        return content;
    }

    // -----------------------------------------------------------------------------------------------------------
    void DestroyBuffer(FileBuffer& buffer)
    {
        delete [] buffer;
        buffer = nullptr;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Binarization
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    namespace Utils
    { 
        // -----------------------------------------------------------------------------------------------------------
        void BinarizeString(FILE* stream, const fastl::string& str)
        { 
            //Perform size encoding in 7bitSize format
            size_t strSize = str.length(); 
            do 
            { 
                const U8 val = strSize < 0x80? strSize & 0x7F : (strSize & 0x7F) | 0x80;
                fwrite(&val,sizeof(U8),1,stream);
                strSize >>= 7;
            }
            while(strSize);

            fwrite(str.c_str(),str.length(),1,stream);
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeU8(FILE* stream, const U8 input)
        { 
            fwrite(&input,sizeof(U8),1,stream);
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeU32(FILE* stream, const U32 input)
        { 
            fwrite(&input,sizeof(U32),1,stream);
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeU64(FILE* stream, const U64 input)
        { 
            fwrite(&input,sizeof(U64),1,stream);
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeUnit(FILE* stream, const CompileUnit unit)
        { 
            BinarizeString(stream,unit.name); 
            for (U32 value : unit.values)
            { 
                BinarizeU32(stream, value);
            }
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeUnits(FILE* stream, const TCompileUnits& units)
        {
            //TODO ~ ramonv ~ check for U32 overflow
            BinarizeU32(stream,static_cast<U32>(units.size()));
            for (const CompileUnit& unit : units)
            { 
                BinarizeUnit(stream,unit);
            }
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeGlobals(FILE* stream, const TCompileDatas& globals)
        {
            //TODO ~ ramonv ~ check for U32 overflow
            BinarizeU32(stream,static_cast<unsigned int>(globals.size()));
            for (const auto& entry : globals)
            { 
                const CompileData& data = entry;
                BinarizeString(stream,entry.name);
                BinarizeU64(stream,data.accumulated);
                BinarizeU32(stream,data.min);
                BinarizeU32(stream,data.max);
                BinarizeU32(stream,data.count);
            }
        }

        // -----------------------------------------------------------------------------------------------------------
        void BinarizeTimelineEvents(FILE* stream, const TCompileEvents& events)
        { 
            BinarizeU32(stream,static_cast<unsigned int>(events.size()));
            for (const CompileEvent& evt : events)
            { 
                BinarizeU32(stream,evt.start);
                BinarizeU32(stream,evt.duration);
                BinarizeU32(stream,static_cast<U32>(evt.nameId)); //TODO ~ ramonv ~ careful with overflows
                BinarizeU8(stream,static_cast<CompileCategoryType>(evt.category));
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class Binarizer::Impl
    {
    public: 
        Impl(const char* _path)
            : path(_path)
            , timelineStream(nullptr)
            , timelineCount(0u)
        {}

        FILE* NextTimelineStream();
        void CloseTimelineStream();

    private: 
        bool AppendTimelineExtension(fastl::string& filename);

    public: 
        const char* path;

    private:
        FILE*  timelineStream; 
        size_t timelineCount;
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // -----------------------------------------------------------------------------------------------------------
    bool Binarizer::Impl::AppendTimelineExtension(fastl::string& filename)
    { 
        size_t extensionNumber = timelineCount / TIMELINES_PER_FILE;

        char digits[TIMELINE_FILE_NUM_DIGITS];
        for(int i=TIMELINE_FILE_NUM_DIGITS-1;i>=0;--i,extensionNumber/=10)
        { 
            digits[i]= (extensionNumber % 10) + '0';
        }

        if (extensionNumber > 0) 
        { 
            LOG_ERROR("Reached timeline file number limit");
            return false; 
        } 

        filename += ".t"; 
        for(int i=0;i<TIMELINE_FILE_NUM_DIGITS;++i)
        { 
            filename += digits[i];
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------
    FILE* Binarizer::Impl::NextTimelineStream()
    {
        if ((timelineCount % TIMELINES_PER_FILE) == 0)
        { 
            CloseTimelineStream();

            fastl::string filename = path;
            if (AppendTimelineExtension(filename))
            { 
                const errno_t result = fopen_s(&timelineStream,filename.c_str(),"wb");
                if (result) 
                { 
                    LOG_ERROR("Unable to create output file %s",filename);
                    timelineStream = nullptr;
                }

                //Add the file header
                Utils::BinarizeU32(timelineStream,SCORE_VERSION);
            }
        }

        ++timelineCount;
        return timelineStream;
    } 

    // -----------------------------------------------------------------------------------------------------------
    void Binarizer::Impl::CloseTimelineStream()
    { 
        if (timelineStream)
        {
            fclose(timelineStream);
            timelineStream = nullptr;
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // -----------------------------------------------------------------------------------------------------------
    Binarizer::Binarizer(const char* path)
        : m_impl( new Impl(path))
    {}

    // -----------------------------------------------------------------------------------------------------------
    Binarizer::~Binarizer()
    { 
        m_impl->CloseTimelineStream();
        delete m_impl;
    }

    // -----------------------------------------------------------------------------------------------------------
    void Binarizer::Binarize(const ScoreData& data)
    { 
        const char* filename = m_impl->path;
        LOG_PROGRESS("Writing to file %s",filename);

        FILE* stream;
        const errno_t result = fopen_s(&stream,filename,"wb");

        if (result) 
        { 
            LOG_ERROR("Unable to create output file!");
            return;
        }

        Utils::BinarizeU32(stream,SCORE_VERSION);

        Utils::BinarizeUnits(stream,data.units);
        for (int i=0;i<ToUnderlying(CompileCategory::GahterCount);++i)
        { 
            Utils::BinarizeGlobals(stream,data.globals[i]);
        }    

        fclose(stream);

        LOG_PROGRESS("Done!");
    }

    // -----------------------------------------------------------------------------------------------------------
    void Binarizer::Binarize(const ScoreTimeline& timeline)
    { 
        //TODO ~ ramonv ~ add option to disable timeline export

        if (FILE* stream = m_impl->NextTimelineStream())
        { 
            Utils::BinarizeTimelineEvents(stream,timeline.events);
            LOG_INFO("Timeline for %s exported", timeline.name.c_str());
        }
    }
}
