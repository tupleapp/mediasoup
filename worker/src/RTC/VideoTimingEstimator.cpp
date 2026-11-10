#define MS_CLASS "VideoTimingEstimator"

#include "RTC/VideoTimingEstimator.hpp"
#include "Logger.hpp"
#include <mutex> // for std::once_flag, std::call_once
#include <string>

#ifdef _WIN32
// windows
#include <windows.h>

using ModuleType = HINSTANCE;

static ModuleType loadModule(const char* x)
{
	return LoadLibrary(x);
}

template<class Tp>
static Tp moduleGetSymbol(ModuleType module, const char* name)
{
	return reinterpret_cast<Tp>(GetProcAddress(module, name));
}
#else
// posix
#include <dlfcn.h>

using ModuleType = void*;

static ModuleType loadModule(const char* x)
{
	// The path comes from an env var set by whoever runs the worker on purpose.
	return dlopen(x, RTLD_LAZY); // NOLINT(clang-analyzer-optin.taint.GenericTaint)
}

template<class Tp>
static Tp moduleGetSymbol(ModuleType module, const char* name)
{
	return reinterpret_cast<Tp>(dlsym(module, name));
}
#endif

static constexpr char EnvModulePath[] = "MEDIASOUP_VIDEOTIMING_MODULE";
static constexpr char SymbolCreate[]  = "CreateVideoTimingEstimator";
static constexpr char SymbolDestroy[] = "DestroyVideoTimingEstimator";

namespace RTC
{
	class DefaultVideoTimingEstimator : public VideoTimingEstimator
	{
	public:
		static void Destroy(VideoTimingEstimator* estimator)
		{
			delete estimator;
		}

		static VideoTimingEstimatorPtr Create()
		{
			return { new DefaultVideoTimingEstimator, &DefaultVideoTimingEstimator::Destroy };
		}

		void Reset() override
		{
			this->rtt = -1;
		}

		void OnRtt(int64_t rtt) override
		{
			if (this->rtt == -1)
			{
				this->rtt = rtt;
				return;
			}

			this->rtt = (this->rtt * 3 + rtt) / 4;
		}

		void OnPacketReceived(int64_t /*sendTimestamp*/, int64_t /*receiveTimestamp*/) override
		{
			// do nothing
		}

		bool OnVideoTiming(
		  int64_t /*sendTimestamp*/, int64_t receiveTimestamp, int64_t& recvClockSyncSendTimestamp) override
		{
			if (this->rtt == -1)
			{
				return false;
			}

			recvClockSyncSendTimestamp = receiveTimestamp - (this->rtt / 2);
			return true;
		}

	private:
		int64_t rtt = -1;
	};

	VideoTimingEstimator::VideoTimingEstimatorPtr VideoTimingEstimator::Create()
	{
		using ModuleCreateEstimatorFunction  = VideoTimingEstimator*();
		using ModuleDestroyEstimatorFunction = void(VideoTimingEstimator*);

		static std::once_flag loadModuleOnce;
		// NOTE: A function type cannot be const, so misc-const-correctness is moot.
		// NOLINTNEXTLINE(misc-const-correctness)
		static ModuleCreateEstimatorFunction* moduleCreateFunctionPtr = nullptr;
		// NOLINTNEXTLINE(misc-const-correctness)
		static ModuleDestroyEstimatorFunction* moduleDestroyFunctionPtr = nullptr;

		std::call_once(
		  loadModuleOnce,
		  [&]()
		  {
			  auto* estimatorLibraryPath = std::getenv(EnvModulePath);

			  if (!estimatorLibraryPath)
			  {
				  MS_DEBUG_TAG(info, "'%s' env not set, using default VideoTimingEstimator", EnvModulePath);
				  return;
			  }

			  auto* mod = loadModule(estimatorLibraryPath);

			  if (!mod)
			  {
				  MS_WARN_TAG(
				    info, "LoadModule('%s') error, using default VideoTimingEstimator", estimatorLibraryPath);
				  return;
			  }

			  auto create = moduleGetSymbol<ModuleCreateEstimatorFunction*>(mod, SymbolCreate);

			  if (!create)
			  {
				  MS_WARN_TAG(
				    info,
				    "ModuleGetSymbol<...>(..., '%s') error, using default VideoTimingEstimator",
				    SymbolCreate);
				  return;
			  }

			  auto destroy = moduleGetSymbol<ModuleDestroyEstimatorFunction*>(mod, SymbolDestroy);

			  if (!destroy)
			  {
				  MS_WARN_TAG(
				    info,
				    "ModuleGetSymbol<...>(..., '%s') error, using default VideoTimingEstimator",
				    SymbolDestroy);
				  return;
			  }

			  moduleCreateFunctionPtr  = create;
			  moduleDestroyFunctionPtr = destroy;
		  });

		if (moduleCreateFunctionPtr == nullptr || moduleDestroyFunctionPtr == nullptr)
		{
			// use default implementation
			return DefaultVideoTimingEstimator::Create();
		}
		else
		{
			auto* impl = moduleCreateFunctionPtr();
			if (impl == nullptr)
			{
				// use default implementation
				MS_WARN_TAG(
				  info,
				  "Error creating VideoTimingEstimator from an external module, switching to the default implementation");
				return DefaultVideoTimingEstimator::Create();
			}

			return { impl, moduleDestroyFunctionPtr };
		}
	}
} // namespace RTC
