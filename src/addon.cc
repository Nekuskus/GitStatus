#include <node.h>
#include <filesystem>
#include <future>

#include <cstdio>
#include <iostream>
#include <windows.h>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>

int SystemCapture(
    std::string         CmdLine,    //Command Line
    std::string         CmdRunDir,  //set to '.' for current directory
    std::string& ListStdOut, //Return List of StdOut
    std::string& ListStdErr, //Return List of StdErr
    uint32_t& RetCode)    //Return Exit Code
{
    int                  Success;
    SECURITY_ATTRIBUTES  security_attributes;
    HANDLE               stdout_rd = INVALID_HANDLE_VALUE;
    HANDLE               stdout_wr = INVALID_HANDLE_VALUE;
    HANDLE               stderr_rd = INVALID_HANDLE_VALUE;
    HANDLE               stderr_wr = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION  process_info;
    STARTUPINFO          startup_info;
    std::thread               stdout_thread;
    std::thread               stderr_thread;

    security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attributes.bInheritHandle = TRUE;
    security_attributes.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&stdout_rd, &stdout_wr, &security_attributes, 0) ||
        !SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0)) {
        return -1;
    }

    if (!CreatePipe(&stderr_rd, &stderr_wr, &security_attributes, 0) ||
        !SetHandleInformation(stderr_rd, HANDLE_FLAG_INHERIT, 0)) {
        if (stdout_rd != INVALID_HANDLE_VALUE) CloseHandle(stdout_rd);
        if (stdout_wr != INVALID_HANDLE_VALUE) CloseHandle(stdout_wr);
        return -2;
    }

    ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&startup_info, sizeof(STARTUPINFO));

    startup_info.cb = sizeof(STARTUPINFO);
    startup_info.hStdInput = 0;
    startup_info.hStdOutput = stdout_wr;
    startup_info.hStdError = stderr_wr;

    if (stdout_rd || stderr_rd)
        startup_info.dwFlags |= STARTF_USESTDHANDLES;

    // Make a copy because CreateProcess needs to modify string buffer
    char      CmdLineStr[MAX_PATH];
    strncpy(CmdLineStr, CmdLine.c_str(), MAX_PATH);
    CmdLineStr[MAX_PATH - 1] = 0;

    Success = CreateProcess(
        nullptr,
        CmdLineStr,
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        CmdRunDir.c_str(),
        &startup_info,
        &process_info
    );
    CloseHandle(stdout_wr);
    CloseHandle(stderr_wr);

    if (!Success) {
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
        CloseHandle(stdout_rd);
        CloseHandle(stderr_rd);
        return -4;
    }
    else {
        CloseHandle(process_info.hThread);
    }

    if (stdout_rd) {
        stdout_thread = std::thread([&]() {
            DWORD  n;
            const size_t bufsize = 1000;
            char         buffer[bufsize];
            for (;;) {
                n = 0;
                int Success = ReadFile(
                    stdout_rd,
                    buffer,
                    (DWORD)bufsize,
                    &n,
                    nullptr
                );
                printf("STDERR: Success:%d n:%d\n", Success, (int)n);
                if (!Success || n == 0)
                    break;
                std::string s(buffer, n);
                printf("STDOUT:(%s)\n", s.c_str());
                ListStdOut += s;
            }
            printf("STDOUT:BREAK!\n");
            });
    }

    if (stderr_rd) {
        stderr_thread = std::thread([&]() {
            DWORD        n;
            const size_t bufsize = 1000;
            char         buffer[bufsize];
            for (;;) {
                n = 0;
                int Success = ReadFile(
                    stderr_rd,
                    buffer,
                    (DWORD)bufsize,
                    &n,
                    nullptr
                );
                printf("STDERR: Success:%d n:%d\n", Success, (int)n);
                if (!Success || n == 0)
                    break;
                std::string s(buffer, n);
                printf("STDERR:(%s)\n", s.c_str());
                ListStdOut += s;
            }
            printf("STDERR:BREAK!\n");
            });
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    if (!GetExitCodeProcess(process_info.hProcess, (DWORD*)&RetCode))
        RetCode = -1;

    CloseHandle(process_info.hProcess);

    if (stdout_thread.joinable())
        stdout_thread.join();

    if (stderr_thread.joinable())
        stderr_thread.join();

    CloseHandle(stdout_rd);
    CloseHandle(stderr_rd);

    return 0;
}

namespace c_internals {

	using v8::Exception;
	using v8::FunctionCallbackInfo;
	using v8::Isolate;
	using v8::Local;
	using v8::Object;
	using v8::String;
	using v8::Value;
	using v8::Array;
	using v8::Context;
	using v8::Integer;

	enum GitStatus {
		clean = 0,
		notclean = 1,
		error = 2
	};

	class GitReturn {
	public:
		std::string foldername;
		GitStatus status;
		std::string errormessage;
	};

	GitReturn rungit(Isolate *isolate, std::filesystem::path path) {

        int rc;
        uint32_t retcode;
        std::string out;
        std::string err;
        std::string args = "git -C " + path.generic_string() + " status --short";
        rc = SystemCapture(
            args,
            ".",
            out,
            err,
            retcode
            );
        if (rc < 0) {
            isolate->ThrowException(Exception::Error(
                String::NewFromUtf8(isolate, std::string("Process creation crashed, error code" + std::to_string(rc)).c_str()).ToLocalChecked()));
            GitReturn ret{ path.generic_string(), GitStatus::error, std::string("Process creation crashed, error code" + std::to_string(rc)) };
        }

        
        GitStatus returnstatus = GitStatus::clean;
        //check if error occured
        std::string errorstring = "fatal:";
        size_t index = out.find(errorstring);
        std::string errormessage = "";
        if (index != std::string::npos) {
            errormessage = out.at(index + 7);
            returnstatus = GitStatus::error;
        } else if (out != "\n") returnstatus = GitStatus::notclean;


		GitReturn ret{path.generic_string(), returnstatus, errormessage};
        return ret;
    }

	void Gitstatus(const FunctionCallbackInfo<Value>& args) {
		Isolate* isolate = args.GetIsolate();
		
		Local<v8::Context> context = v8::Context::New(isolate);

		std::filesystem::path path = std::filesystem::current_path();

		if (args.Length() == 0) {
			// current directory
		}
		else if (args.Length() == 1) {
			if (!(args[0]->IsString())) {
				isolate->ThrowException(Exception::TypeError(
					String::NewFromUtf8(isolate, "Argument is not String").ToLocalChecked()));
				return;
			}
			String::Utf8Value str(isolate, args[0]);
			path = std::filesystem::path(*str);
		}
		else {
			isolate->ThrowException(Exception::TypeError(
				String::NewFromUtf8(isolate, "Wrong number of arguments, pass nothing or 1 String").ToLocalChecked()));
			return;
		}

		std::vector<std::future<GitReturn>> futures;
		for (const auto& dir : std::filesystem::directory_iterator(path)) {
			futures.push_back(std::async(std::launch::async, &rungit, isolate, dir));
		}
		
		std::vector<GitReturn> results;
		int index = 0;
		for (; index < futures.size(); index++) {
			results.push_back(futures[index].get());
		}
		auto arr = Array::New(isolate, index * 3);
		
		size_t i = 0;
		for (size_t i3 = 0; i3 < arr->Length();i3+=3)
		{
			i++;
			Local<String> folder;
			Local<String> stat;
			Local<String> err;
			String::NewFromUtf8(isolate, results[i].foldername.c_str()).ToLocal(&folder);
			Integer::New(isolate, results[i].status);
			String::NewFromUtf8(isolate, results[i].errormessage.c_str()).ToLocal(&err);
			arr->Set(context, i3, folder);
			arr->Set(context, i3+1, stat);
			arr->Set(context, i3+2, err);
		}

		args.GetReturnValue().Set(arr);
	}

	void Initlialize(Local<Object> exports) {
		NODE_SET_METHOD(exports, "gitstatus", Gitstatus);
	}
	NODE_MODULE(NODE_GYP_MODULE_NAME, Initlialize)

}