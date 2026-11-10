#include "common.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"
#include "RTC/RTP/Packet.hpp"
#include "RTC/RTP/RtpStream.hpp"
#include "RTC/RTP/RtpStreamRecv.hpp"
#include "Utils.hpp"
#include "mocks/include/MockShared.hpp"
#include <catch2/catch_test_macros.hpp>

// Tuple: http://www.webrtc.org/experiments/rtp-hdrext/video-timing support.
SCENARIO("video-timing RTP header extension", "[rtp][videotiming]")
{
	constexpr uint8_t VideoTimingId{ 3 };
	constexpr uint32_t Ssrc{ 1111 };

	alignas(4) uint8_t factoryBuffer[1500];
	alignas(4) uint8_t cloneBuffer[1500];

	// flags, encode start, encode end, packetization, pacer exit, network1, network2.
	const auto makePacket =
	  [&](uint16_t pacerExit, uint16_t network1, uint16_t network2) -> std::unique_ptr<RTC::RTP::Packet>
	{
		std::unique_ptr<RTC::RTP::Packet> packet(
		  RTC::RTP::Packet::Factory(factoryBuffer, sizeof(factoryBuffer)));

		REQUIRE(packet);

		packet->SetSsrc(Ssrc);
		packet->SetPayloadType(100);
		packet->SetSequenceNumber(1);
		// 10 s at 90 kHz.
		packet->SetTimestamp(900000);

		alignas(4) uint8_t value[13];

		Utils::Byte::Set1Byte(value, 0, 1);
		Utils::Byte::Set2Bytes(value, 1, 2);
		Utils::Byte::Set2Bytes(value, 3, 5);
		Utils::Byte::Set2Bytes(value, 5, 6);
		Utils::Byte::Set2Bytes(value, 7, pacerExit);
		Utils::Byte::Set2Bytes(value, 9, network1);
		Utils::Byte::Set2Bytes(value, 11, network2);

		const std::vector<RTC::RTP::Packet::Extension> extensions{
			{ RTC::RtpHeaderExtensionUri::Type::VIDEO_TIMING, VideoTimingId, 13, value }
		};

		REQUIRE(packet->SetExtensions(RTC::RTP::Packet::ExtensionsType::OneByte, extensions));

		RTC::RTP::HeaderExtensionIds headerExtensionIds;

		headerExtensionIds.videoTiming = VideoTimingId;

		packet->AssignExtensionIds(headerExtensionIds);

		return packet;
	};

	struct VideoTiming
	{
		uint8_t flags{ 0 };
		uint16_t encodeStart{ 0 };
		uint16_t encodeEnd{ 0 };
		uint16_t packetization{ 0 };
		uint16_t pacerExit{ 0 };
		uint16_t network1{ 0 };
		uint16_t network2{ 0 };
	};

	const auto read = [](const RTC::RTP::Packet* packet) -> VideoTiming
	{
		VideoTiming t;

		REQUIRE(packet->ReadVideoTiming(
		  t.flags, t.encodeStart, t.encodeEnd, t.packetization, t.pacerExit, t.network1, t.network2));

		return t;
	};

	SECTION("SetExtensions() assigns the video-timing id")
	{
		auto packet = makePacket(20, 0, 0);

		RTC::RTP::HeaderExtensionIds none;

		packet->AssignExtensionIds(none);

		VideoTiming t;

		REQUIRE(!packet->ReadVideoTiming(
		  t.flags, t.encodeStart, t.encodeEnd, t.packetization, t.pacerExit, t.network1, t.network2));

		// The Producer mangles packets with SetExtensions(), which must leave the id set.
		alignas(4) uint8_t value[13]{};
		const std::vector<RTC::RTP::Packet::Extension> extensions{
			{ RTC::RtpHeaderExtensionUri::Type::VIDEO_TIMING,
			 static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::VIDEO_TIMING),
			 13, value }
		};

		REQUIRE(packet->SetExtensions(RTC::RTP::Packet::ExtensionsType::OneByte, extensions));
		REQUIRE(read(packet.get()).pacerExit == 0);
	}

	SECTION("SFU enter and exit times are written relative to the estimated capture time")
	{
		auto packet = makePacket(20, 0, 0);

		const auto t = read(packet.get());

		REQUIRE(t.flags == 1);
		REQUIRE(t.encodeStart == 2);
		REQUIRE(t.encodeEnd == 5);
		REQUIRE(t.packetization == 6);
		REQUIRE(t.pacerExit == 20);
		REQUIRE(t.network1 == 0);
		REQUIRE(t.network2 == 0);

		// Without an estimated capture time nothing is written.
		REQUIRE(!packet->UpdateVideoTimingSfuEnterTime(5000));
		REQUIRE(!packet->UpdateVideoTimingSfuExitTime(5000));
		REQUIRE(read(packet.get()).network1 == 0);

		packet->SetEstimatedCaptureTimeMs(5000);

		REQUIRE(packet->UpdateVideoTimingSfuEnterTime(5040));
		REQUIRE(packet->UpdateVideoTimingSfuExitTime(5055));
		REQUIRE(read(packet.get()).network1 == 40);
		REQUIRE(read(packet.get()).network2 == 55);
		REQUIRE(read(packet.get()).pacerExit == 20);

		// Times before the capture or beyond 16 bits are not written.
		REQUIRE(!packet->UpdateVideoTimingSfuExitTime(4999));
		REQUIRE(!packet->UpdateVideoTimingSfuExitTime(5000 + 65536));
		REQUIRE(read(packet.get()).network2 == 55);

		// A clone (as stored for retransmission) keeps the estimated capture time.
		std::unique_ptr<RTC::RTP::Packet> clone(packet->Clone(cloneBuffer, sizeof(cloneBuffer)));

		REQUIRE(clone->GetEstimatedCaptureTimeMs() == 5000);
		REQUIRE(clone->UpdateVideoTimingSfuExitTime(5070));
		REQUIRE(read(clone.get()).network2 == 70);
		REQUIRE(read(packet.get()).network2 == 55);
	}

	SECTION("RtpStreamRecv estimates the capture time once the RTT is known")
	{
		class Listener : public RTC::RTP::RtpStreamRecv::Listener
		{
		public:
			void OnRtpStreamScore(
			  RTC::RTP::RtpStream* /*rtpStream*/, uint8_t /*score*/, uint8_t /*previousScore*/) override
			{
			}
			void OnRtpStreamSendRtcpPacket(
			  RTC::RTP::RtpStreamRecv* /*rtpStream*/, RTC::RTCP::Packet* /*packet*/) override
			{
			}
			uint8_t OnRtpStreamNeedWorstRemoteFractionLost(RTC::RTP::RtpStreamRecv* /*rtpStream*/) override
			{
				return 0;
			}
			void OnRtpStreamSpatialLayerActivityChanged(
			  RTC::RTP::RtpStreamRecv* /*rtpStream*/, uint8_t /*spatialLayer*/, bool /*isActive*/) override
			{
			}
		};

		mocks::MockShared shared(/*getTimeUs*/
		                         []() -> int64_t
		                         {
			                         return 1000 * 1000;
		                         });
		Listener listener;
		RTC::RTP::RtpStream::Params params;

		params.ssrc      = Ssrc;
		params.clockRate = 90000;
		params.mimeType.SetMimeType("video/VP9");

		RTC::RTP::RtpStreamRecv rtpStream(
		  std::addressof(listener), std::addressof(shared), params, /*sendNackDelayMs*/ 0, false);

		constexpr int64_t ReceivedAtMs{ 30000 };

		// No RTT yet, so the default estimator has nothing to say.
		{
			auto packet = makePacket(20, 0, 0);

			REQUIRE(rtpStream.ReceivePacket(packet.get(), ReceivedAtMs * 1000));
			REQUIRE(packet->GetEstimatedCaptureTimeMs() == 0);
			REQUIRE(read(packet.get()).network1 == 0);
		}

		// RTT of 500 ms: the Receiver Reference Time at 10 s is answered right away
		// and the answer arrives at 10.5 s.
		RTC::RTCP::DelaySinceLastRr::SsrcInfo ssrcInfo;

		ssrcInfo.SetSsrc(Ssrc);
		ssrcInfo.SetLastReceiverReport(0x000A0000);
		ssrcInfo.SetDelaySinceLastReceiverReport(0);

		REQUIRE(rtpStream.ReceiveRtcpXrDelaySinceLastRr(std::addressof(ssrcInfo), 10500000) == 500.0f);

		// From the producer peer: it left the producer 20 ms after capture and
		// took RTT/2 to arrive, so it entered the SFU 270 ms after capture.
		{
			auto packet = makePacket(20, 0, 0);

			packet->SetSequenceNumber(2);

			REQUIRE(rtpStream.ReceivePacket(packet.get(), ReceivedAtMs * 1000));
			REQUIRE(packet->GetEstimatedCaptureTimeMs() == ReceivedAtMs - 250 - 20);
			REQUIRE(read(packet.get()).network1 == 270);
			REQUIRE(read(packet.get()).network2 == 0);
		}

		// From another SFU node (network fields set): it left that node network2 ms
		// after capture, and network1 is that node's and stays untouched.
		{
			auto packet = makePacket(20, 30, 45);

			packet->SetSequenceNumber(3);

			REQUIRE(rtpStream.ReceivePacket(packet.get(), ReceivedAtMs * 1000));
			REQUIRE(packet->GetEstimatedCaptureTimeMs() == ReceivedAtMs - 250 - 45);
			REQUIRE(read(packet.get()).network1 == 30);
			REQUIRE(read(packet.get()).network2 == 45);
		}
	}

	SECTION("audio streams don't estimate")
	{
		class Listener : public RTC::RTP::RtpStreamRecv::Listener
		{
		public:
			void OnRtpStreamScore(
			  RTC::RTP::RtpStream* /*rtpStream*/, uint8_t /*score*/, uint8_t /*previousScore*/) override
			{
			}
			void OnRtpStreamSendRtcpPacket(
			  RTC::RTP::RtpStreamRecv* /*rtpStream*/, RTC::RTCP::Packet* /*packet*/) override
			{
			}
			uint8_t OnRtpStreamNeedWorstRemoteFractionLost(RTC::RTP::RtpStreamRecv* /*rtpStream*/) override
			{
				return 0;
			}
			void OnRtpStreamSpatialLayerActivityChanged(
			  RTC::RTP::RtpStreamRecv* /*rtpStream*/, uint8_t /*spatialLayer*/, bool /*isActive*/) override
			{
			}
		};

		mocks::MockShared shared(/*getTimeUs*/
		                         []() -> int64_t
		                         {
			                         return 1000 * 1000;
		                         });
		Listener listener;
		RTC::RTP::RtpStream::Params params;

		params.ssrc      = Ssrc;
		params.clockRate = 48000;
		params.mimeType.SetMimeType("audio/opus");

		RTC::RTP::RtpStreamRecv rtpStream(
		  std::addressof(listener), std::addressof(shared), params, /*sendNackDelayMs*/ 0, false);

		RTC::RTCP::DelaySinceLastRr::SsrcInfo ssrcInfo;

		ssrcInfo.SetSsrc(Ssrc);
		ssrcInfo.SetLastReceiverReport(0x000A0000);
		ssrcInfo.SetDelaySinceLastReceiverReport(0);
		rtpStream.ReceiveRtcpXrDelaySinceLastRr(std::addressof(ssrcInfo), 10500000);

		auto packet = makePacket(20, 0, 0);

		REQUIRE(rtpStream.ReceivePacket(packet.get(), 30000 * 1000));
		REQUIRE(packet->GetEstimatedCaptureTimeMs() == 0);
	}
}
