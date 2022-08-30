#include <node.h>
#include <filesystem>
#include <future>

namespace c_internals {

	using v8::Exception;
	using v8::FunctionCallbackInfo;
	using v8::Isolate;
	using v8::Local;
	using v8::Object;
	using v8::String;
	using v8::Value;
	using v8::Array;

	enum GitStatus {
		clean,
		notclean,
		error
	};

	class GitReturn {
	public:
		std::string foldername;
		GitStatus status;
		std::string errormessage;
	};

	GitReturn rungit(std::filesystem::path path) {

		GitReturn ret{ path.generic_string(), GitStatus::clean /*replace later*/, ""};
	}

	void Gitstatus(const FunctionCallbackInfo<Value>& args) {
		Isolate* isolate = args.GetIsolate();
		
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
			futures.push_back(std::async(std::launch::async, & rungit, dir));
		}
		
		std::vector<GitReturn> results;
		for (int i = 0; i < futures.size(); i++) {
			results.push_back(futures[i].get());
		}

		//args.GetReturnValue().Set(Array::New())
	}

	void Initlialize(Local<Object> exports) {
		NODE_SET_METHOD(exports, "gitstatus", Gitstatus);
	}
	NODE_MODULE(NODE_GYP_MODULE_NAME, Initlialize)

}