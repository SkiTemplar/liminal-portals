#include "Debug/LiminalGpuProfile.h"

#include "GPUProfiler.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

#if RHI_NEW_GPU_PROFILER && WITH_RHI_BREADCRUMBS
#define LIMINAL_GPU_PROFILE 1
#else
#define LIMINAL_GPU_PROFILE 0
#endif

#if LIMINAL_GPU_PROFILE

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalGpuProfileDetail
{
	using UE::RHI::GPUProfiler::FEventSink;
	using UE::RHI::GPUProfiler::FEventStream;
	using UE::RHI::GPUProfiler::FQueue;
	using FGpuEvent = UE::RHI::GPUProfiler::FEvent;

	/**
	 * Sumidero de eventos de GPU. Reproduce la contabilidad de tiempo ocupado del sumidero de ProfileGPU
	 * (GPUProfiler.cpp, FGPUProfilerSink_StatSystem): cada marca de tiempo de miga o de fin de trabajo
	 * suma el intervalo desde la anterior a las migas abiertas. Las esperas entre colas caen entre un
	 * FEndWork y el FBeginWork siguiente, que no suma, así que no hace falta resolver vallas.
	 */
	class FSink final : public FEventSink
	{
	public:
		void InitializeQueues(TConstArrayView<FQueue> Queues) override
		{
			FScopeLock Lock(&Mutex);
			for (const FQueue& Queue : Queues)
			{
				QueueStates.FindOrAdd(Queue.Value);
			}
		}

		void ProcessStreams(TConstArrayView<TSharedRef<FEventStream>> EventStreams) override
		{
			FScopeLock Lock(&Mutex);
			for (const TSharedRef<FEventStream>& Stream : EventStreams)
			{
				FQueueState& State = QueueStates.FindOrAdd(Stream->Queue.Value);
				if (State.Prefix.IsEmpty())
				{
					State.Prefix = FString::Printf(TEXT("%s%d"), Stream->Queue.GetTypeString(), Stream->Queue.Index);
					State.bGraphics = Stream->Queue.Type == FQueue::EType::Graphics && Stream->Queue.Index == 0;
				}
				FIterator Iterator(Stream);
				while (const FGpuEvent* Event = Iterator.Pop())
				{
					ProcessEvent(State, *Event);
				}
			}
		}

		void Begin()
		{
			FScopeLock Lock(&Mutex);
			Accumulated.Reset();
			QueueBusy.Reset();
			Frames = 0;
			++Epoch;
			bRecording = true;
		}

		LiminalGpuProfile::FSnapshot End()
		{
			FScopeLock Lock(&Mutex);
			bRecording = false;
			LiminalGpuProfile::FSnapshot Snapshot;
			Snapshot.Frames = Frames;
			for (const TPair<FString, uint64>& Entry : QueueBusy)
			{
				Snapshot.QueueBusyMs.Add(Entry.Key, FPlatformTime::ToMilliseconds64(Entry.Value));
			}
			Snapshot.Paths.Reserve(Accumulated.Num());
			for (const TPair<FString, FAccumulated>& Entry : Accumulated)
			{
				LiminalGpuProfile::FPathTime& Path = Snapshot.Paths.AddDefaulted_GetRef();
				Path.Path = Entry.Key;
				Path.InclusiveMs = FPlatformTime::ToMilliseconds64(Entry.Value.Cycles);
				Path.Count = Entry.Value.Count;
			}
			Snapshot.Paths.Sort([](const LiminalGpuProfile::FPathTime& A, const LiminalGpuProfile::FPathTime& B)
			{
				return A.Path < B.Path;
			});
			return Snapshot;
		}

	private:
		struct FStackEntry
		{
			uint32 Id = 0;
			/** Ruta completa (cola incluida); en las migas por debajo de MaxDepth, la del antecesor. */
			FString Path;
			/** Fuera de la grabación no se formatean los nombres (el hilo de eventos corre en cada fotograma). */
			bool bNamed = false;
			bool bRecorded = false;
			/** Grabación en la que se abrió: solo cuenta si se abre y se cierra en la misma. */
			uint32 Epoch = 0;
			uint64 BusyAtBegin = 0;
		};

		struct FQueueState
		{
			FString Prefix;
			bool bGraphics = false;
			TArray<FStackEntry> Stack;
			uint64 LastGpuCycles = 0;
			/** Tiempo ocupado acumulado de la cola desde el arranque. */
			uint64 TotalBusy = 0;
		};

		struct FAccumulated
		{
			uint64 Cycles = 0;
			int32 Count = 0;
		};

		void AddBusy(FQueueState& State, uint64 Timestamp)
		{
			if (Timestamp > State.LastGpuCycles)
			{
				const uint64 Busy = Timestamp - State.LastGpuCycles;
				State.TotalBusy += Busy;
				if (bRecording)
				{
					QueueBusy.FindOrAdd(State.Prefix) += Busy;
				}
			}
			State.LastGpuCycles = FMath::Max(State.LastGpuCycles, Timestamp);
		}

		void PushEntry(FQueueState& State, const FRHIBreadcrumbNode* Node, bool bCountable)
		{
			FStackEntry Entry;
			Entry.Id = Node->ID;
			Entry.BusyAtBegin = State.TotalBusy;
			Entry.Epoch = Epoch;
			if (!bRecording)
			{
				State.Stack.Add(MoveTemp(Entry));
				return;
			}
			Entry.bNamed = true;
			const int32 Depth = State.Stack.Num() + 1;
			const FString& ParentPath = State.Stack.IsEmpty() ? State.Prefix : State.Stack.Last().Path;
			if (Depth <= LiminalGpuProfile::MaxDepth)
			{
				FRHIBreadcrumbNode::FBuffer Buffer;
				Entry.Path = ParentPath + TEXT("/") + LiminalGpuProfile::NormalizeBreadcrumbName(Node->GetTCHAR(Buffer));
				Entry.bRecorded = bCountable;
			}
			else
			{
				Entry.Path = ParentPath;
			}
			State.Stack.Add(MoveTemp(Entry));
		}

		/** Rehace la pila desde la raíz hasta Node (incluido) cuando no casa con los eventos vistos. */
		void RebuildStack(FQueueState& State, const FRHIBreadcrumbNode* Node)
		{
			TArray<const FRHIBreadcrumbNode*, TInlineAllocator<32>> Chain;
			for (const FRHIBreadcrumbNode* Current = Node; Current && Current != FRHIBreadcrumbNode::Sentinel;
				Current = Current->GetParent())
			{
				Chain.Add(Current);
			}
			State.Stack.Reset();
			for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
			{
				// Abiertas antes de que se viera su comienzo: su tiempo no es completo y no se cuenta.
				PushEntry(State, Chain[Index], false);
			}
		}

		void PopEntry(FQueueState& State)
		{
			const FStackEntry Entry = State.Stack.Pop(EAllowShrinking::No);
			if (bRecording && Entry.bRecorded && Entry.Epoch == Epoch)
			{
				FAccumulated& Total = Accumulated.FindOrAdd(Entry.Path);
				Total.Cycles += State.TotalBusy - Entry.BusyAtBegin;
				++Total.Count;
			}
		}

		void ProcessEvent(FQueueState& State, const FGpuEvent& Event)
		{
			switch (Event.GetType())
			{
			case FGpuEvent::EType::BeginWork:
				State.LastGpuCycles = FMath::Max(State.LastGpuCycles, Event.Value.Get<FGpuEvent::FBeginWork>().GPUTimestampTOP);
				break;
			case FGpuEvent::EType::EndWork:
				AddBusy(State, Event.Value.Get<FGpuEvent::FEndWork>().GPUTimestampBOP);
				break;
			case FGpuEvent::EType::BeginBreadcrumb:
			{
				const FGpuEvent::FBeginBreadcrumb& Begin = Event.Value.Get<FGpuEvent::FBeginBreadcrumb>();
				AddBusy(State, Begin.GPUTimestampTOP);
				const FRHIBreadcrumbNode* Parent = Begin.Breadcrumb->GetParent();
				const bool bParentMatches = (Parent == nullptr || Parent == FRHIBreadcrumbNode::Sentinel)
					? State.Stack.IsEmpty() : (!State.Stack.IsEmpty() && State.Stack.Last().Id == Parent->ID);
				const bool bParentUnnamed = bRecording && !State.Stack.IsEmpty() && !State.Stack.Last().bNamed;
				if (!bParentMatches || bParentUnnamed)
				{
					RebuildStack(State, Parent);
				}
				PushEntry(State, Begin.Breadcrumb, true);
				break;
			}
			case FGpuEvent::EType::EndBreadcrumb:
			{
				const FGpuEvent::FEndBreadcrumb& End = Event.Value.Get<FGpuEvent::FEndBreadcrumb>();
				AddBusy(State, End.GPUTimestampBOP);
				if (State.Stack.IsEmpty() || State.Stack.Last().Id != End.Breadcrumb->ID)
				{
					RebuildStack(State, End.Breadcrumb);
				}
				PopEntry(State);
				break;
			}
			case FGpuEvent::EType::FrameBoundary:
				if (bRecording && State.bGraphics)
				{
					++Frames;
				}
				break;
			default:
				break;
			}
		}

		FCriticalSection Mutex;
		TMap<uint32, FQueueState> QueueStates;
		TMap<FString, FAccumulated> Accumulated;
		TMap<FString, uint64> QueueBusy;
		int32 Frames = 0;
		uint32 Epoch = 0;
		bool bRecording = false;
	};

	/**
	 * Se registra al cargar el módulo, como el sumidero de ProfileGPU: GetSinks() del RHI no tiene
	 * cerrojo, así que registrarlo a mitad de partida competiría con el hilo que procesa los eventos.
	 */
	FSink GSink;
}

namespace LiminalGpuProfile
{
	bool IsAvailable()
	{
		return true;
	}

	void BeginRecording()
	{
		LiminalGpuProfileDetail::GSink.Begin();
	}

	FSnapshot EndRecording()
	{
		return LiminalGpuProfileDetail::GSink.End();
	}
}

#else

namespace LiminalGpuProfile
{
	bool IsAvailable()
	{
		return false;
	}

	void BeginRecording()
	{
	}

	FSnapshot EndRecording()
	{
		return FSnapshot();
	}
}

#endif

namespace LiminalGpuProfile
{
	FString NormalizeBreadcrumbName(const FString& Name)
	{
		const FString FramePrefix(TEXT("Frame "));
		const FString Rest = Name.Mid(FramePrefix.Len());
		if (Name.StartsWith(FramePrefix, ESearchCase::CaseSensitive) && !Rest.IsEmpty() && Rest.IsNumeric())
		{
			return TEXT("Frame");
		}
		FString Result = Name;
		Result.ReplaceCharInline(TEXT('/'), TEXT('|'));
		return Result;
	}
}
