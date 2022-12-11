#pragma once

#include <functional>
#include <list>

namespace skygfx
{
    inline void hash_combine(std::size_t& seed) { }

    template <typename T, typename... Rest>
    inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        hash_combine(seed, rest...);
    }
}

#define SKYGFX_MAKE_HASHABLE(type, ...) \
    namespace std {\
        template<> struct hash<type> {\
            std::size_t operator()(const type &t) const {\
                std::size_t ret = 0;\
                skygfx::hash_combine(ret, __VA_ARGS__);\
                return ret;\
            }\
        };\
    }

class ExecuteList
{
public:
    using Func = std::function<void()>;

public:
    ~ExecuteList()
    {
        flush();
    }

public:
    void add(Func func) 
    { 
        mFuncs.push_back(func); 
    }

    void flush()
    {
        for (auto func : mFuncs)
        {
            func();
        }

        mFuncs.clear();
    }

private:
    std::list<Func> mFuncs;
};

template<class... Ts> struct cases : Ts... { using Ts::operator()...; };
template<class... Ts> cases(Ts...) -> cases<Ts...>;
