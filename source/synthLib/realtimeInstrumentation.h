#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace synthLib
{
	enum class RealtimeEventKind { PanelInput, PanelInputResult, PanelDelivery, HostTransport };
	struct RealtimeEvent
	{
		RealtimeEventKind kind = RealtimeEventKind::PanelInput;
		uint64_t timeNanoseconds = 0, sequence = 0, callbackIndex = 0, inputId = 0;
		uint32_t model = 0, command = 0, argument = 0;
		bool accepted = false, deferred = false;
		bool playing = false, offline = false, bypassed = false, transportKnown = false, initial = false;
	};

	// One callback's correlated timings. All durations are nanoseconds; outer
	// timings include inner work. Inter-callback spacing is context, not an xrun.
	struct RealtimeSlowCallback
	{
		uint64_t index = 0, startNanoseconds = 0, durationNanoseconds = 0;
		uint64_t budgetNanoseconds = 0, interCallbackNanoseconds = 0;
		uint64_t synthNanoseconds = 0, lockWaitNanoseconds = 0;
		uint64_t resamplerNanoseconds = 0, deviceNanoseconds = 0;
		uint64_t deferredNanoseconds = 0, liveJitCompilations = 0, deferredJitCompilations = 0;
		uint32_t frames = 0, outputBuses = 0, outputChannels = 0;
		uint32_t midiEvents = 0, midiBytes = 0, deviceSampleRate = 0;
		uint32_t resamplerMode = 0, dspClockPercent = 100;
		double sampleRate = 0;
		bool bypassed = false, playing = false, offline = false;
		bool resamplingActive = false, dualMachine = false;
	};

	struct RealtimeInstrumentationSnapshot
	{
		bool enabled = false;
		uint64_t offlineCallbackCount = 0;
		uint64_t slowCallbacksDropped = 0;
		uint64_t timelineEvents = 0, timelineEventsDropped = 0;
		// Realtime callback duration/budget: <25%, <50%, <75%, <100%, <150%, >=150%.
		std::array<uint64_t, 6> realtimeBudgetHistogram{};
		uint64_t outerHostCallbackCount = 0;
		uint64_t outerHostCallbackNanoseconds = 0;
		uint64_t outerHostCallbackMaxNanoseconds = 0;
		// Diagnostic deadline comparison only, excluding offline callbacks. These
		// estimates are not the host or audio driver's actual xrun count.
		uint64_t outerHostCallbackOverrunCount = 0;
		uint64_t outerHostCallbackMaxOverrunNanoseconds = 0;
		uint64_t callbacksWithJitCompilation = 0;
		uint64_t bypassedCallbackCount = 0;
		uint64_t synthProcessCount = 0;
		uint64_t synthProcessNanoseconds = 0;
		uint64_t synthProcessMaxNanoseconds = 0;
		uint64_t synthProcessLockWaitNanoseconds = 0;
		uint64_t synthProcessLockWaitMaxNanoseconds = 0;
		// Time a non-realtime caller (e.g. the UI/message thread via
		// Plugin::withDeviceLocked) spent waiting for the same device lock the
		// realtime audio thread holds for the whole of Plugin::process(). A
		// sustained rise here on the message thread, in step with realtime
		// budget overruns, is the signature of UI starvation caused by lock
		// contention with an overrunning audio callback (not a UI-side hang).
		uint64_t controlLockWaitCount = 0;
		uint64_t controlLockWaitNanoseconds = 0;
		uint64_t controlLockWaitMaxNanoseconds = 0;
		// Non-blocking probe counters: how often a non-realtime caller (the
		// iOS diagnostics UI's poll timer) could vs. could not immediately
		// acquire the same device lock, without ever waiting for it. A
		// rising failure count alongside otherwise-normal scheduler/DSP
		// numbers is the signature of UI starvation from lock contention,
		// as distinct from the emulator itself running slow.
		uint64_t uiTryLockSuccessCount = 0;
		uint64_t uiTryLockFailureCount = 0;
		// Diagnostic for the "does the scheduler build a cycle backlog after
		// firmware becomes valid" question: one sample per Hardware::advance()
		// call (i.e. once per host audio chunk), recorded only while a device
		// is actually valid/emulating. If ucCyclesExecuted is ever less than
		// what that chunk's frame count required, work was deferred instead of
		// completed in-callback - that is the runaway/backlog signature. If it
		// never happens, the scheduler always finishes the exact amount of
		// work the real hardware clock requires, no more, no less, and a slow
		// callback is purely an interpreter-speed problem, not a scheduling bug.
		uint64_t schedulerAdvanceCount = 0;
		uint64_t schedulerMachineFrames = 0;
		uint64_t schedulerUcCyclesExecuted = 0;
		uint64_t schedulerUcCyclesMax = 0;
		uint64_t schedulerUcUnderrunCount = 0;
		uint64_t schedulerDsp1CyclesExecuted = 0;
		uint64_t schedulerDsp1CyclesMax = 0;
		uint64_t schedulerDsp2CyclesExecuted = 0;
		uint64_t schedulerDsp2CyclesMax = 0;
		uint64_t schedulerNanoseconds = 0;
		uint64_t schedulerMaxNanoseconds = 0;
		uint64_t resamplerCallCount = 0;
		uint64_t resamplerNanoseconds = 0;
		uint64_t resamplerMaxNanoseconds = 0;
		uint64_t resamplerHostFrames = 0;
		uint64_t resamplingActiveCallbackCount = 0;
		uint64_t deviceProcessNanoseconds = 0;
		uint64_t deviceProcessMaxNanoseconds = 0;
		uint64_t jitCompilationCount = 0;
		uint64_t liveJitCompilationCount = 0;
		uint64_t deferredCandidateJitCompilationCount = 0;
		uint64_t deferredDualMachineCallbackCount = 0;
		uint64_t deferredCandidateAdvanceCount = 0;
		uint64_t deferredCandidateFrames = 0;
		uint64_t deferredCandidateNanoseconds = 0;
		uint64_t deferredCandidateMaxNanoseconds = 0;
		uint64_t callbacksWithAuxOutputBuses = 0;
		uint32_t latestActiveOutputBuses = 0;
		uint32_t maximumActiveOutputBuses = 0;
		uint32_t latestActiveOutputChannels = 0;
		uint32_t maximumActiveOutputChannels = 0;
	};

	// Opt-in counters for diagnosing work performed on the host audio callback.
	// Enable before audio starts with GEARMULATOR_RT_INSTRUMENTATION=1, or use
	// setEnabled() through Plugin::getRealtimeInstrumentation(). No reporting is
	// performed by this class. The MD/MM diagnostics writer drains records off-thread.
	// Disabled operation is a relaxed atomic load and a predictable branch at each
	// instrumented outer boundary. Clock reads and counter writes happen only while
	// explicitly enabled.
	class RealtimeInstrumentation final
	{
	public:
		class CallbackScope final
		{
		public:
			CallbackScope(RealtimeInstrumentation& _owner, size_t _frames,
				double _sampleRate, bool _bypassed = false) noexcept;
			~CallbackScope();

			CallbackScope(const CallbackScope&) = delete;
			CallbackScope& operator=(const CallbackScope&) = delete;

			bool isActive() const noexcept { return m_owner != nullptr; }
			void setActiveOutputLayout(uint32_t _buses, uint32_t _channels) noexcept;
			void setHostState(bool _playing, bool _offline, bool _transportKnown = true) noexcept;
			void setMidiInputSummary(uint32_t _events, uint32_t _bytes) noexcept;

		private:
			RealtimeInstrumentation* m_owner = nullptr;
			uint64_t m_startNanoseconds = 0;
			uint64_t m_budgetNanoseconds = 0;
			uint32_t m_activeOutputBuses = 0;
			uint32_t m_activeOutputChannels = 0;
			uint32_t m_frames = 0, m_midiEvents = 0, m_midiBytes = 0;
			double m_sampleRate = 0;
			bool m_bypassed = false, m_playing = false, m_offline = false;
		};

		class DeferredCandidateScope final
		{
		public:
			explicit DeferredCandidateScope(uint32_t _frames) noexcept;
			~DeferredCandidateScope();

			DeferredCandidateScope(const DeferredCandidateScope&) = delete;
			DeferredCandidateScope& operator=(const DeferredCandidateScope&) = delete;

		private:
			RealtimeInstrumentation* m_owner = nullptr;
			uint64_t m_startNanoseconds = 0;
			uint32_t m_frames = 0;
			bool m_previousCandidateRole = false;
		};

		RealtimeInstrumentation() noexcept;

		void setEnabled(bool _enabled) noexcept;
		bool isEnabled() const noexcept
		{
			return m_enabled.load(std::memory_order_relaxed);
		}
		void reset() noexcept;
		// Single off-audio-thread consumer. Does not reset the queue while the
		// producer is active; a full queue drops new records rather than waiting.
		bool popSlowCallback(RealtimeSlowCallback& _callback) noexcept;
		// Separate bounded MPSC timeline: producers make one nonblocking attempt.
		// Contention/full capacity drops an event and is counted, never waited on.
		static constexpr size_t TimelineCapacity = 1024;
		struct PanelInputToken { uint64_t id = 0, epoch = 0; };
		PanelInputToken beginPanelInput(uint32_t _model, uint8_t _command, uint8_t _argument) noexcept;
		void endPanelInput(PanelInputToken _token, uint32_t _model, uint8_t _command,
			uint8_t _argument, bool _accepted) noexcept;
		static void recordCurrentPanelDelivery(uint32_t _model, uint8_t _command, uint8_t _argument) noexcept;
		bool popTimelineEvent(RealtimeEvent& _event) noexcept;
		static constexpr size_t SlowCallbackCapacity = 512;
		static void setCurrentDeviceContext(uint32_t _rate, uint32_t _mode,
			uint32_t _clockPercent) noexcept;
		// Each field is race-free during concurrent recording/reset, but the result
		// is intentionally a relaxed, non-transactional diagnostic snapshot.
		RealtimeInstrumentationSnapshot snapshot() const noexcept;

		void recordSynthProcess(uint64_t _nanoseconds) noexcept;
		void recordSynthProcessLockWait(uint64_t _nanoseconds) noexcept;
		// Called from Plugin::withDeviceLocked, i.e. from whatever thread asks
		// for the device (normally the UI/message thread), never from the
		// realtime audio callback itself.
		void recordControlLockWait(uint64_t _nanoseconds) noexcept;
		// Called only by a non-blocking device-lock probe (never the
		// blocking withDeviceLocked, never the realtime audio callback).
		// See Plugin::tryLockDeviceForDiagnostics.
		void recordUiTryLock(bool _acquired) noexcept;
		// Cheap check so a caller (e.g. md::Hardware::advance()) can skip taking
		// its own diagnostic timestamps/cycle snapshots entirely when nothing
		// is capturing this callback.
		static bool isCurrentCallbackCaptureActive() noexcept;
		// Called once per md::Hardware::advance() (i.e. once per host audio
		// chunk), from deep inside mdLib, using the same "current callback"
		// context as recordCurrentPanelDelivery below - never from every DSP
		// instruction, so this stays cheap enough for a release build.
		static void recordCurrentSchedulerAdvance(bool _firmwareValid,
			uint32_t _machineFrames, uint64_t _ucCyclesRequested,
			uint64_t _ucCyclesExecuted, uint64_t _dsp1CyclesExecuted,
			uint64_t _dsp2CyclesExecuted, uint64_t _nanoseconds) noexcept;
		void recordResampler(uint64_t _nanoseconds, uint64_t _deviceNanoseconds,
			size_t _hostFrames, bool _resamplingActive) noexcept;

		// Called only from the MD DSP JIT's per-block configuration callback. An
		// event is attributed only when a host callback scope is active on this thread.
		static void recordCurrentCallbackJitCompilation() noexcept;

	private:
		friend class CallbackScope;
		friend class DeferredCandidateScope;

		void recordHostCallback(RealtimeSlowCallback _callback) noexcept;
		friend struct RealtimeInstrumentationTestAccess;
		void recordDeferredCandidate(uint32_t _frames, uint64_t _nanoseconds) noexcept;
		bool recordTimelineEvent(RealtimeEvent _event) noexcept;
		void recordHostTransport(bool _playing, bool _offline, bool _bypassed, bool _known) noexcept;

		static_assert(std::atomic<uint64_t>::is_always_lock_free
			&& std::atomic<bool>::is_always_lock_free,
			"Performance capture requires lock-free atomics");
		struct Slot
		{
			std::atomic<bool> ready{false};
			RealtimeSlowCallback callback;
		};
		std::array<Slot, SlowCallbackCapacity> m_slowCallbacks{};
		struct EventSlot { std::atomic<bool> ready{false}; RealtimeEvent event; };
		std::array<EventSlot, TimelineCapacity> m_timeline{};
		std::atomic_flag m_eventProducer = ATOMIC_FLAG_INIT;
		size_t m_eventWritePosition = 0; // protected by the nonblocking producer gate
		size_t m_eventReadPosition = 0; // sole report consumer
		std::atomic<uint64_t> m_timelineEvents{0}, m_timelineEventsDropped{0};
		std::atomic<uint64_t> m_nextInputId{1}, m_captureEpoch{1};
		uint64_t m_transportEpoch = 0; // audio callback thread only
		uint32_t m_lastTransportFlags = 0;
		// Host callbacks are serialized per instance. Only the producer accesses
		// write position, only the report worker accesses read position.
		size_t m_writePosition = 0, m_readPosition = 0;
		uint64_t m_previousCallbackStart = 0;
		std::atomic<uint64_t> m_offlineCallbackCount{0}, m_slowCallbacksDropped{0};
		std::array<std::atomic<uint64_t>, 6> m_realtimeBudgetHistogram{};
		std::atomic<bool> m_enabled{false};
		std::atomic<uint64_t> m_outerHostCallbackCount{0};
		std::atomic<uint64_t> m_outerHostCallbackNanoseconds{0};
		std::atomic<uint64_t> m_outerHostCallbackMaxNanoseconds{0};
		std::atomic<uint64_t> m_outerHostCallbackOverrunCount{0};
		std::atomic<uint64_t> m_outerHostCallbackMaxOverrunNanoseconds{0};
		std::atomic<uint64_t> m_callbacksWithJitCompilation{0};
		std::atomic<uint64_t> m_bypassedCallbackCount{0};
		std::atomic<uint64_t> m_synthProcessCount{0};
		std::atomic<uint64_t> m_synthProcessNanoseconds{0};
		std::atomic<uint64_t> m_synthProcessMaxNanoseconds{0};
		std::atomic<uint64_t> m_synthProcessLockWaitNanoseconds{0};
		std::atomic<uint64_t> m_synthProcessLockWaitMaxNanoseconds{0};
		std::atomic<uint64_t> m_controlLockWaitCount{0};
		std::atomic<uint64_t> m_controlLockWaitNanoseconds{0};
		std::atomic<uint64_t> m_controlLockWaitMaxNanoseconds{0};
		std::atomic<uint64_t> m_uiTryLockSuccessCount{0};
		std::atomic<uint64_t> m_uiTryLockFailureCount{0};
		std::atomic<uint64_t> m_schedulerAdvanceCount{0};
		std::atomic<uint64_t> m_schedulerMachineFrames{0};
		std::atomic<uint64_t> m_schedulerUcCyclesExecuted{0};
		std::atomic<uint64_t> m_schedulerUcCyclesMax{0};
		std::atomic<uint64_t> m_schedulerUcUnderrunCount{0};
		std::atomic<uint64_t> m_schedulerDsp1CyclesExecuted{0};
		std::atomic<uint64_t> m_schedulerDsp1CyclesMax{0};
		std::atomic<uint64_t> m_schedulerDsp2CyclesExecuted{0};
		std::atomic<uint64_t> m_schedulerDsp2CyclesMax{0};
		std::atomic<uint64_t> m_schedulerNanoseconds{0};
		std::atomic<uint64_t> m_schedulerMaxNanoseconds{0};
		std::atomic<uint64_t> m_resamplerCallCount{0};
		std::atomic<uint64_t> m_resamplerNanoseconds{0};
		std::atomic<uint64_t> m_resamplerMaxNanoseconds{0};
		std::atomic<uint64_t> m_resamplerHostFrames{0};
		std::atomic<uint64_t> m_resamplingActiveCallbackCount{0};
		std::atomic<uint64_t> m_deviceProcessNanoseconds{0};
		std::atomic<uint64_t> m_deviceProcessMaxNanoseconds{0};
		std::atomic<uint64_t> m_jitCompilationCount{0};
		std::atomic<uint64_t> m_liveJitCompilationCount{0};
		std::atomic<uint64_t> m_deferredCandidateJitCompilationCount{0};
		std::atomic<uint64_t> m_deferredDualMachineCallbackCount{0};
		std::atomic<uint64_t> m_deferredCandidateAdvanceCount{0};
		std::atomic<uint64_t> m_deferredCandidateFrames{0};
		std::atomic<uint64_t> m_deferredCandidateNanoseconds{0};
		std::atomic<uint64_t> m_deferredCandidateMaxNanoseconds{0};
		std::atomic<uint64_t> m_callbacksWithAuxOutputBuses{0};
		std::atomic<uint32_t> m_latestActiveOutputBuses{0};
		std::atomic<uint32_t> m_maximumActiveOutputBuses{0};
		std::atomic<uint32_t> m_latestActiveOutputChannels{0};
		std::atomic<uint32_t> m_maximumActiveOutputChannels{0};
	};
}
