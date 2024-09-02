#include "pch.h"

#include "scheduler.h"

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
	(void)ud;
	(void)osize;
	if (nsize == 0) {
		free(ptr);
		return NULL;
	} else
		return realloc(ptr, nsize);
}

std::string beautify_stack_trace(std::string stack_trace) {
	std::string colored;
	size_t current = 0;
	while (current < stack_trace.size()) {
		size_t newline = stack_trace.find_first_of('\n', current);
		size_t colon = stack_trace.find_first_of(':', current);
		//printf("%s\n", stack_trace.substr(current, 3).c_str());
		if (stack_trace.substr(current, 3) == "[C]") {
			size_t space = stack_trace.find_first_of(' ', current + 3);
			size_t function = stack_trace.find_first_of(" function ", current + 3);
			colored += FUNCTION_COLOR + "function[C]:";
			if (function == std::string::npos) {
				colored += "<unnamed>";
			} else {
				current = function + strlen(" function ");
				size_t ending = newline == std::string::npos ? stack_trace.size() : newline + 1;
				colored += stack_trace.substr(current, ending - current);
			}
			if (newline == std::string::npos) {
				break;
			}
			current = newline + 1;
			continue;
		}
		colored += MAGENTA + stack_trace.substr(current, colon - current + 1);
		current = colon + 1;
		size_t space = stack_trace.find_first_of(' ', current);
		if (space == std::string::npos) {
			colored += NUMBER_COLOR + stack_trace.substr(current);
			break;
		} else if (newline != std::string::npos && space > newline) {
			space = newline - 1;
		}
		colored += NUMBER_COLOR + stack_trace.substr(current, space - current) + MAGENTA + ": ";
		current = space + 1;
		size_t function = stack_trace.find_first_of(" function ", current);
		colored += FUNCTION_COLOR + "function[Lua]:";
		if (function == std::string::npos || function > newline) {
			colored += "<unnamed>";
		} else {
			current = function + strlen(" function ");
			size_t ending = newline == std::string::npos ? stack_trace.size() : newline;
			colored += stack_trace.substr(current, ending - current);
		}
		if (newline == std::string::npos) {
			break;
		}
		colored += '\n';
		current = newline + 1;
	}
	return colored.substr(0, colored.size() - 1) + RESET;
}

API size_t luau::thread_count = 0;
void userthread_callback(lua_State* parent_thread, lua_State* thread) {
	if (parent_thread) {
		//printf("Thread created: %lld -> %lld\n", luau::thread_count, luau::thread_count + 1);
		luau::thread_count++;
	} else {
		//printf("Thread destroyed: %lld -> %lld\n", luau::thread_count, luau::thread_count - 1);
		luau::thread_count--;
	}
}
void set_callbacks(lua_State* thread) {
	lua_Callbacks* callbacks = lua_callbacks(thread);
	//printf("Existing: %p\n", callbacks->userthread);
	callbacks->userthread = userthread_callback;
}

API lua_State* luau::create_state() {
	lua_State* state = lua_newstate(l_alloc, NULL);
	set_callbacks(state);
	if (Luau::CodeGen::isSupported()) {
		Luau::CodeGen::create(state);
	}
	luaL_openlibs(state);
	return state;
}
API lua_State* luau::create_thread(lua_State* thread) {
	lua_State* new_thread = lua_newthread(thread);
	//luaL_sandboxthread(thread);
	return new_thread;
}
API void luau::load(lua_State* thread, const std::string& bytecode) {
	int status = luau_load(thread, "=runluau", bytecode.data(), bytecode.size(), 0);
	if (status != 0) [[unlikely]] {
		if (status != 1) [[unlikely]] {
			throw std::runtime_error(std::format("Unknown luau_load status `{}`\n", status));
		}
		const char* error_message = lua_tostring(thread, 1);
		throw std::runtime_error(std::format("Syntax error:\n{}\n", error_message));
	}

	if (Luau::CodeGen::isSupported()) {
		Luau::CodeGen::CompilationOptions options{ .flags = Luau::CodeGen::CodeGen_OnlyNativeModules | Luau::CodeGen::CodeGen_ColdFunctions };
		Luau::CodeGen::compile(thread, -1, options);
	}
}
lua_State* main_thread = nullptr;
API void luau::add_thread_to_resume_queue(lua_State* thread, lua_State* from, int args, std::function<void()> setup_func) {
	if (!main_thread) {
		main_thread = thread;
	}
	scheduler::add_thread_to_resume_queue(thread, from, args, setup_func);
}
API void luau::resume_and_handle_status(lua_State* thread, lua_State* from, int args, std::function<void()> setup_func) {
	if (!from || lua_costatus(from, thread) == LUA_COSUS) {
		//printf("Beginning\n");
		setup_func();
		int status = lua_resume(thread, from, args);
		//printf("End\n");
		switch (status) {
		case LUA_OK: case LUA_YIELD: break;
		case LUA_ERRRUN:
			printf("Script errored:\n" RED "%s" RESET "\nStack trace:\n%s\n", lua_tostring(thread, -1), beautify_stack_trace(lua_debugtrace(thread)).c_str());
			//printf("Script errored. Stack trace:\n%s\n", lua_debugtrace(thread));
			break;
		default: printf("Unknown status: %d\n", status);
		}
		if (status != LUA_YIELD || (from && lua_costatus(from, thread) == LUA_COSUS)) {
			lua_resetthread(thread);
		}
	}
}

void luau::start_scheduler() {
	HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (!timer) {
		DWORD error = GetLastError();
		printf("Failed to CreateWaitableTimer: %d\n", error);
		exit(error);
	}
	LARGE_INTEGER frequency, start, end;
	QueryPerformanceFrequency(&frequency);
	LARGE_INTEGER gc_start, gc_end;
	size_t i = 0;
	while (true) {
		QueryPerformanceCounter(&start);
		//printf("Frame %lld\n", ++i);
		scheduler::cycle();
		QueryPerformanceCounter(&end);
		//QueryPerformanceCounter(&gc_start);
		lua_gc(main_thread, LUA_GCSTEP, 1000);
		//QueryPerformanceCounter(&gc_end);
		//printf("GC in %f\n", static_cast<double>(gc_end.QuadPart - gc_start.QuadPart) / frequency.QuadPart);
		if (luau::thread_count <= 1) {
			//printf("Thread count: %lld\nMain status: %d\n", luau::thread_count, lua_status(main_thread));
			if (lua_status(main_thread) != LUA_YIELD) {
				break;
			}
		}
		double delta_time = static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart;
		double time_left = (1.0 / (SCHEDULER_RATE * 2.0)) - delta_time;
		//printf("%f - %f = %f\n", (1.0 / SCHEDULER_RATE), delta_time, time_left);
		if (time_left > 0) {
			LARGE_INTEGER due_time = { .QuadPart = -static_cast<LONGLONG>(time_left * 10000000.0) };
			SetWaitableTimer(timer, &due_time, 0, nullptr, nullptr, false);
			WaitForSingleObject(timer, INFINITE);
		}
	}
	//printf("Scheduler stopped\n");
}

API void signal_yield_ready(HANDLE yield_ready_event) {
	if (!SetEvent(yield_ready_event)) {
		DWORD error = GetLastError();
		printf("Failed to SetEvent: %d\n", error);
		exit(error);
	}
}
API void create_windows_thread_for_luau(lua_State* thread, void(*func)(lua_State* thread, HANDLE yield_ready_event, void* ud), void* ud) {
	HANDLE yield_ready_event = CreateEvent(nullptr, false, false, nullptr);
	std::thread win_thread(func, thread, yield_ready_event, ud);
	win_thread.detach();
	WaitForSingleObject(yield_ready_event, INFINITE);
}

bool APIENTRY DllMain(HMODULE, DWORD reason, void* reserved) {
	return true;
}