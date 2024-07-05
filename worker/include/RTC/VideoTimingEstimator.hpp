#ifndef MS_RTC_VIDEO_TIMING_ESTIMATOR_HPP
#define MS_RTC_VIDEO_TIMING_ESTIMATOR_HPP

#include <inttypes.h>
#ifndef MS_EXTENSION
#include <memory> // for unique_ptr
#endif

namespace RTC
{
	class VideoTimingEstimator
	{
	public:
		virtual void Reset()                                                           = 0;
		virtual void OnRtt(int64_t rtt)                                                = 0;
		virtual void OnPacketReceived(int64_t sendTimestamp, int64_t receiveTimestamp) = 0;
		virtual bool OnVideoTiming(
		  int64_t sendTimestamp, int64_t receiveTimestamp, int64_t& recvClockSyncSendTimestamp) = 0;
		virtual ~VideoTimingEstimator() = default;

#ifndef MS_EXTENSION
		// unique pointer with custom deleter
		using VideoTimingEstimatorPtr =
		  std::unique_ptr<VideoTimingEstimator, void (*)(VideoTimingEstimator*)>;
		static VideoTimingEstimatorPtr Create();
#endif
	};
} // namespace RTC

// Exported symbol for extension builds
#ifdef MS_EXTENSION

#ifdef _WIN32
#define MS_EXTENSION_EXPORTS __declspec(dllexport)
#else
#define MS_EXTENSION_EXPORTS __attribute__((visibility("default")))
#endif

extern "C" MS_EXTENSION_EXPORTS RTC::VideoTimingEstimator* CreateVideoTimingEstimator();
extern "C" MS_EXTENSION_EXPORTS void DestroyVideoTimingEstimator(RTC::VideoTimingEstimator* estimator);
#endif

#endif
