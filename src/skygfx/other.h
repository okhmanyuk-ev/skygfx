#pragma once

#include <functional>
#include <list>

namespace skygfx
{
	inline void hash_combine(std::size_t& seed) { }

	template <typename T, typename... Rest>
	inline void hash_combine(std::size_t& seed, const T& value, Rest... rest)
	{
		std::hash<T> hasher;
		seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		hash_combine(seed, rest...);
	}

	template <typename T, typename... Rest>
	inline void hash_combine(std::size_t& seed, const std::vector<T>& values, Rest... rest)
	{
		for (const auto& value : values)
		{
			hash_combine(seed, value);
		}
		hash_combine(seed, rest...);
	}

	template <typename T, typename U, typename... Rest>
	inline void hash_combine(std::size_t& seed, const std::unordered_map<T, U>& values, Rest... rest)
	{
		for (const auto& [key, value] : values)
		{
			hash_combine(seed, key);
			hash_combine(seed, value);
		}
		hash_combine(seed, rest...);
	}
}

#define SKYGFX_MAKE_HASHABLE(T, ...) \
	namespace std {\
		template<> struct hash<T> {\
			std::size_t operator()(const T& t) const {\
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
