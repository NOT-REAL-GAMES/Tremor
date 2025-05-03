// Copyright 2025 NOT REAL GAMES
//
// Permission is hereby granted, free of charge, 
// to any person obtaining a copy of this software 
// and associated documentation files(the "Software"), 
// to deal in the Software without restriction, 
// including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or 
// sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions:
//
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-
// INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN 
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
// OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
// IN THE SOFTWARE.

// This file is part of Tremor: a game engine built on a 
// rewrite of the Quake Engine as a foundation. The original 
// Quake Engine is licensed under the GPLv2 license. 

// The Tremor project is not affiliated with or endorsed by id Software.
// idTech 2's dependencies on Quake will be gradually phased out of the Tremor project. 

#include "main.h"

class Engine {
public:

	static inline int FindLastBitNonZero(const uint32_t mask)
	{
		unsigned long result;
		_BitScanReverse(&result, mask);
		return result;
	}

	static inline uint32_t Q_log2(uint32_t val)
	{
		assert(val > 0);
		return FindLastBitNonZero(val);
	}

	static inline uint32_t Q_nextPow2(uint32_t val)
	{
		uint32_t result = 1;
		if (val > 1)
			result = 1 << (FindLastBitNonZero(val - 1) + 1);
		return result;
	}

	uint64_t ticks;

	int			numgltextures;
	gltexture_t* active_gltextures, * free_gltextures;
	gltexture_t* notexture, * nulltexture, * whitetexture, * greytexture, * greylightmap, * bluenoisetexture;


	cvar_t r_lodbias = { "r_lodbias", "1", CVAR_ARCHIVE };
	cvar_t gl_lodbias = { "gl_lodbias", "0", CVAR_ARCHIVE };
	cvar_t r_scale = { "r_scale", "1", CVAR_ARCHIVE };
	cvar_t vid_fullscreen = { "vid_fullscreen", "0", CVAR_ARCHIVE }; // QuakeSpasm, was "1"
	cvar_t vid_width = { "vid_width", "1280", CVAR_ARCHIVE };		  // QuakeSpasm, was 640
	cvar_t vid_height = { "vid_height", "720", CVAR_ARCHIVE };		  // QuakeSpasm, was 480
	cvar_t vid_refreshrate = { "vid_refreshrate", "60", CVAR_ARCHIVE };
	cvar_t vid_vsync = { "vid_vsync", "0", CVAR_ARCHIVE };
	cvar_t vid_desktopfullscreen = { "vid_desktopfullscreen", "0", CVAR_ARCHIVE }; // QuakeSpasm
	cvar_t vid_borderless = { "vid_borderless", "0", CVAR_ARCHIVE };				// QuakeSpasm
	cvar_t		  vid_palettize = { "vid_palettize", "0", CVAR_ARCHIVE };
	cvar_t		  vid_filter = { "vid_filter", "0", CVAR_ARCHIVE };
	cvar_t		  vid_anisotropic = { "vid_anisotropic", "0", CVAR_ARCHIVE };
	cvar_t		  vid_fsaa = { "vid_fsaa", "0", CVAR_ARCHIVE };
	cvar_t		  vid_fsaamode = { "vid_fsaamode", "0", CVAR_ARCHIVE };
	cvar_t		  vid_gamma = { "gamma", "0.9", CVAR_ARCHIVE };		// johnfitz -- moved here from view.c
	cvar_t		  vid_contrast = { "contrast", "1.4", CVAR_ARCHIVE }; // QuakeSpasm, MarkV
	cvar_t		  r_usesops = { "r_usesops", "1", CVAR_ARCHIVE };		// johnfitz
#if defined(_DEBUG)
	cvar_t r_raydebug = { "r_raydebug", "0", CVAR_NONE };
#endif

	class Tasks {
	public:
		Engine* engine;
		int					 num_workers;
		SDL_Thread** worker_threads;
		task_t				 the_tasks[MAX_PENDING_TASKS];
		task_queue_t* free_task_queue;
		task_queue_t* executable_task_queue;
		task_counter_t* indexed_task_counters;
		uint8_t				 steal_worker_indices[TASKS_MAX_WORKERS * 2];

		void Initialize() {
			num_workers = SDL_GetCPUCount();
			worker_threads = (SDL_Thread**)SDL_calloc(num_workers, sizeof(SDL_Thread*));
			free_task_queue = CreateTaskQueue(MAX_PENDING_TASKS);
			executable_task_queue = CreateTaskQueue(MAX_PENDING_TASKS);
			indexed_task_counters = (task_counter_t*)SDL_calloc(num_workers * MAX_PENDING_TASKS, sizeof(task_counter_t));
			for (int i = 0; i < num_workers; ++i)
			{
				steal_worker_indices[i] = i;
				steal_worker_indices[num_workers + i] = (i + 1) % num_workers;
			}
		}

		Tasks() {
			//empty constructor for wrapper class
		}

		Tasks(Engine e){
			engine = &e;
			engine->tasks = this;
			
			Initialize();
			Tasks_Init();
		}

		static inline void CPUPause()
		{
			// Don't have to actually check for SSE2 support, the
			// instruction is backwards compatible and executes as a NOP
			_mm_pause();
		}

		static inline int IndexedTaskCounterIndex(int task_index, int worker_index)
		{
			return (MAX_PENDING_TASKS * worker_index) + task_index;
		}

		static inline void SpinWaitSemaphore(SDL_sem* semaphore)
		{
			int remaining_spins = WAIT_SPIN_COUNT;
			int result = 0;
			int i = 0;
				

			while ((result = SDL_SemTryWait(semaphore)) != 0)
			{
				CPUPause();
				if (--remaining_spins == 0)
					break;
			}
			if (result != 0) {
				SDL_SemWait(semaphore);
			}
		}

		static uint32_t ShuffleIndex(uint32_t i)
		{
			// Swap bits 0-3 and 4-7 to avoid false sharing
			return (i & ~0xFF) | ((i & 0xF) << 4) | ((i >> 4) & 0xF);
		}

		static inline uint32_t TaskQueuePop(task_queue_t* queue)
		{
			SpinWaitSemaphore(queue->pop_semaphore);
			uint32_t tail = Atomic_LoadUInt32(&queue->tail);
			bool cas_successful = false;
			do
			{
				const uint32_t next = (tail + 1u) & queue->capacity_mask;
				cas_successful = Atomic_CompareExchangeUInt32(&queue->tail, &tail, next);
			} while (!cas_successful);

			const uint32_t shuffled_index = ShuffleIndex(tail);
			while (Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) == 0u)
				CPUPause();

			const uint32_t val = Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) - 1;
			Atomic_StoreUInt32(&queue->task_indices[shuffled_index], 0u);
			SDL_SemPost(queue->push_semaphore);
			ANNOTATE_HAPPENS_AFTER(&queue->task_indices[shuffled_index]);

			return val;
		}



		static inline void TaskQueuePush(task_queue_t* queue, uint32_t task_index)
		{
			SpinWaitSemaphore(queue->push_semaphore);
			uint32_t head = Atomic_LoadUInt32(&queue->head);
			bool cas_successful = false;
			do
			{
				const uint32_t next = (head + 1u) & queue->capacity_mask;
				cas_successful = Atomic_CompareExchangeUInt32(&queue->head, &head, next);
			} while (!cas_successful);

			const uint32_t shuffled_index = ShuffleIndex(head);
			while (Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) != 0u)
				CPUPause();

			ANNOTATE_HAPPENS_BEFORE(&queue->task_indices[shuffled_index]);
			Atomic_StoreUInt32(&queue->task_indices[shuffled_index], task_index + 1);
			SDL_SemPost(queue->pop_semaphore);
		}

		inline void Task_ExecuteIndexed(int worker_index, task_t* task, uint32_t task_index)
		{
			for (int i = 0; i < num_workers; ++i)
			{
				const int		steal_worker_index = steal_worker_indices[worker_index + i];
				int				counter_index = IndexedTaskCounterIndex(task_index, steal_worker_index);
				task_counter_t* counter = &indexed_task_counters[counter_index];
				uint32_t		index = 0;
				while ((index = Atomic_IncrementUInt32(&counter->index)) < counter->limit)
				{
					((task_indexed_func_t)task->func) (index, task->payload);
				}
			}
		}

		static inline uint32_t IndexFromTaskHandle(task_handle_t handle)
		{
			return handle & (MAX_PENDING_TASKS - 1);
		}

		/*
		====================
		EpochFromTaskHandle
		====================
		*/
		static inline uint64_t EpochFromTaskHandle(task_handle_t handle)
		{
			return handle >> NUM_INDEX_BITS;
		}


		void Task_Submit(task_handle_t handle)
		{
			uint32_t task_index = IndexFromTaskHandle(handle);
			task_t* task = &the_tasks[task_index];
			assert(task->epoch == EpochFromTaskHandle(handle));
			ANNOTATE_HAPPENS_BEFORE(task);
			if (Atomic_DecrementUInt32(&task->remaining_dependencies) == 1)
			{
				const int num_task_workers = (task->task_type == TASK_TYPE_INDEXED) ? min(task->indexed_limit, num_workers) : 1;
				Atomic_StoreUInt32(&task->remaining_workers, num_task_workers);
				for (int i = 0; i < num_task_workers; ++i)
				{
					TaskQueuePush(executable_task_queue, task_index);
				}
			}
		}

		static task_queue_t* CreateTaskQueue(int capacity)
		{
			assert(capacity > 0);
			assert((capacity & (capacity - 1)) == 0); // Needs to be power of 2
			task_queue_t* queue = (task_queue_t*)Mem_Alloc(sizeof(task_queue_t) + (sizeof(a::atomic_uint32_t) * (capacity - 1)));
			queue->capacity_mask = capacity - 1;
			queue->push_semaphore = SDL_CreateSemaphore(capacity - 1);
			queue->pop_semaphore = SDL_CreateSemaphore(0);
			return queue;
		}

		int Task_Worker(Engine* engine,void* data)
		{
			is_worker = true;

			const int worker_index = (intptr_t)data;
			tl_worker_index = worker_index;
			while (true)
			{
				uint32_t task_index = TaskQueuePop(engine->tasks->executable_task_queue);
				task_t* task = &the_tasks[task_index];
				ANNOTATE_HAPPENS_AFTER(task);

				if (task->task_type == TASK_TYPE_SCALAR)
				{
					((task_func_t)task->func) (task->payload);
				}
				else if (task->task_type == TASK_TYPE_INDEXED)
				{
					Task_ExecuteIndexed(worker_index, task, task_index);
				}

#if defined(USE_HELGRIND)
				ANNOTATE_HAPPENS_BEFORE(task);
				qboolean indexed_task = task->task_type == TASK_TYPE_INDEXED;
				if (indexed_task)
				{
					// Helgrind needs to know about all threads
					// that participated in an indexed execution
					SDL_LockMutex(task->epoch_mutex);
					for (int i = 0; i < task->num_dependents; ++i)
					{
						const int task_index = IndexFromTaskHandle(task->dependent_task_handles[i]);
						task_t* dep_task = &tasks[task_index];
						ANNOTATE_HAPPENS_BEFORE(dep_task);
					}
				}
#endif

				if (Atomic_DecrementUInt32(&task->remaining_workers) == 1)
				{
					SDL_LockMutex(task->epoch_mutex);
					for (int i = 0; i < task->num_dependents; ++i)
						Task_Submit(task->dependent_task_handles[i]);
					task->epoch += 1;
					SDL_CondBroadcast(task->epoch_condition);
					SDL_UnlockMutex(task->epoch_mutex);
					TaskQueuePush(free_task_queue, task_index);
				}

#if defined(USE_HELGRIND)
				if (indexed_task)
					SDL_UnlockMutex(task->epoch_mutex);
#endif
			}
			return 0;
		}

		typedef struct {
			void* data;
			Engine* engine;
		} chucklenuts;

		// REALLY hacky, but this works
		static int Wrapper(void* data){
			auto bla = (chucklenuts*)data;
			Engine* engine = bla->engine;
			engine->tasks->Task_Worker(engine, bla->data);
			return 0;
		}

		void Tasks_Init(void)
		{
			free_task_queue = CreateTaskQueue(MAX_PENDING_TASKS);
			executable_task_queue = CreateTaskQueue(MAX_EXECUTABLE_TASKS);

			for (uint32_t task_index = 0; task_index < (MAX_PENDING_TASKS - 1); ++task_index)
			{
				TaskQueuePush(free_task_queue, task_index);
			}

			for (uint32_t task_index = 0; task_index < MAX_PENDING_TASKS; ++task_index)
			{
				the_tasks[task_index].epoch_mutex = SDL_CreateMutex();
				the_tasks[task_index].epoch_condition = SDL_CreateCond();
			}

			float f = 1.0f;
			num_workers = CLAMP(&f, SDL_GetCPUCount(), TASKS_MAX_WORKERS);

			// Fill lookup table to avoid modulo in Task_ExecuteIndexed
			for (int i = 0; i < num_workers; ++i)
			{
				steal_worker_indices[i] = i;
				steal_worker_indices[i + num_workers] = i;
			}


			indexed_task_counters = (task_counter_t*)Mem_Alloc(sizeof(task_counter_t) * num_workers * MAX_PENDING_TASKS);
			worker_threads = (SDL_Thread**)Mem_Alloc(sizeof(SDL_Thread*) * num_workers);
			for (int i = 0; i < num_workers; ++i)
			{
				chucklenuts bla{};
				bla.data = (void*)(intptr_t)i;
				bla.engine = engine;
				worker_threads[i] = SDL_CreateThread(Wrapper, "Task_Worker", &bla);
			}
			SDL_Log("Created %d worker threads.\n", num_workers);
		}


	};

	class REN {
	public:
		Engine* engine;
		REN(Engine e) {
			engine = &e;
			engine->ren = this;
		}
		

		void R_SubmitStagingBuffer(int index)
		{
			while (engine->gl->sbuf->num_stagings_in_flight > 0)
				SDL_CondWait(engine->gl->sbuf->staging_cond, engine->gl->sbuf->staging_mutex);

			ZEROED_STRUCT(VkMemoryBarrier, memory_barrier);
			memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			vkCmdPipelineBarrier(
				engine->gl->sbuf->staging_buffers[index].command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);

			vkEndCommandBuffer(engine->gl->sbuf->staging_buffers[index].command_buffer);

			ZEROED_STRUCT(VkMappedMemoryRange, range);
			range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			range.memory = engine->gl->sbuf->staging_memory.handle;
			range.size = VK_WHOLE_SIZE;
			vkFlushMappedMemoryRanges(vulkan_globals.device, 1, &range);

			ZEROED_STRUCT(VkSubmitInfo, submit_info);
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &engine->gl->sbuf->staging_buffers[index].command_buffer;

			vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, engine->gl->sbuf->staging_buffers[index].fence);

			engine->gl->sbuf->staging_buffers[index].submitted = true;
			engine->gl->sbuf->current_staging_buffer = (engine->gl->sbuf->current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
		}


		void R_SubmitStagingBuffers(void)
		{
			SDL_LockMutex(engine->gl->staging_mutex);

			while (engine->gl->sbuf->num_stagings_in_flight > 0)
				SDL_CondWait(engine->gl->sbuf->staging_cond, engine->gl->sbuf->staging_mutex);

			int i;
			for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
			{
				if (!engine->gl->sbuf->staging_buffers[i].submitted && engine->gl->sbuf->staging_buffers[i].current_offset > 0)
					R_SubmitStagingBuffer(i);
			}

			SDL_UnlockMutex(engine->gl->sbuf->staging_mutex);
		}

		void R_FlushStagingCommandBuffer(stagingbuffer_t* staging_buffer)
		{
			VkResult err;

			if (!staging_buffer->submitted)
				return;

			err = vkWaitForFences(vulkan_globals.device, 1, &staging_buffer->fence, VK_TRUE, UINT64_MAX);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkWaitForFences failed");

			err = vkResetFences(vulkan_globals.device, 1, &staging_buffer->fence);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkResetFences failed");

			staging_buffer->current_offset = 0;
			staging_buffer->submitted = false;

			ZEROED_STRUCT(VkCommandBufferBeginInfo, command_buffer_begin_info);
			command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			err = vkBeginCommandBuffer(staging_buffer->command_buffer, &command_buffer_begin_info);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBeginCommandBuffer failed");
		}

		void R_FreeVulkanMemory(vulkan_memory_t* memory, a::atomic_uint32_t* num_allocations)
		{
			if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
				Atomic_SubUInt64(&engine->gl->total_device_vulkan_allocation_size, memory->size);
			else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
				Atomic_SubUInt64(&engine->gl->total_host_vulkan_allocation_size, memory->size);
			if (memory->type != VULKAN_MEMORY_TYPE_NONE)
			{
				vkFreeMemory(vulkan_globals.device, memory->handle, NULL);
				if (num_allocations)
					(num_allocations--);
			}
			memory->handle = VK_NULL_HANDLE;
			memory->size = 0;
		}

		void R_DestroyStagingBuffers(void)
		{
			int i;

			R_FreeVulkanMemory(&engine->gl->sbuf->staging_memory, NULL);
			for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
			{
				vkDestroyBuffer(vulkan_globals.device, engine->gl->sbuf->staging_buffers[i].buffer, NULL);
			}
		}

		void R_CreateStagingBuffers()
		{
			int		 i;
			VkResult err;

			ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
			buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_create_info.size = vulkan_globals.staging_buffer_size;
			buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

			for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
			{
				engine->gl->sbuf->staging_buffers[i].current_offset = 0;
				engine->gl->sbuf->staging_buffers[i].submitted = false;

				err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &engine->gl->sbuf->staging_buffers[i].buffer);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateBuffer failed");

				engine->gl->SetObjectName((uint64_t)engine->gl->sbuf->staging_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Staging Buffer");
			}

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(vulkan_globals.device, engine->gl->sbuf->staging_buffers[0].buffer, &memory_requirements);
			const size_t aligned_size = (memory_requirements.size + memory_requirements.alignment);

			ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
			memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
			memory_allocate_info.memoryTypeIndex =
				engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

			R_AllocateVulkanMemory(&engine->gl->sbuf->staging_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &engine->gl->num_vulkan_misc_allocations);
			engine->gl->SetObjectName((uint64_t)engine->gl->sbuf->staging_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Staging Buffers");

			for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
			{
				err = vkBindBufferMemory(vulkan_globals.device, engine->gl->sbuf->staging_buffers[i].buffer, engine->gl->sbuf->staging_memory.handle, i * aligned_size);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");
			}

			void* data;
			err = vkMapMemory(vulkan_globals.device, engine->gl->sbuf->staging_memory.handle, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");

			for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				engine->gl->sbuf->staging_buffers[i].data = (unsigned char*)data + (i * aligned_size);
		}



		byte* R_StagingAllocate(int size, int alignment, VkCommandBuffer* command_buffer, VkBuffer* buffer, int* buffer_offset)
		{
			SDL_LockMutex(engine->gl->staging_mutex);

			while (engine->gl->num_stagings_in_flight > 0)
				SDL_CondWait(engine->gl->staging_cond, engine->gl->staging_mutex);

			vulkan_globals.device_idle = false;

			if (size > vulkan_globals.staging_buffer_size)
			{
				R_SubmitStagingBuffers();

				for (int i = 0; i < NUM_STAGING_BUFFERS; ++i)
					R_FlushStagingCommandBuffer(&engine->gl->sbuf->staging_buffers[i]);

				vulkan_globals.staging_buffer_size = size;

				R_DestroyStagingBuffers();
				R_CreateStagingBuffers();
			}

			stagingbuffer_t* staging_buffer = &engine->gl->sbuf->staging_buffers[engine->gl->sbuf->current_staging_buffer];
			assert(alignment == Q_nextPow2(alignment));
			staging_buffer->current_offset = (staging_buffer->current_offset + alignment);

			if ((staging_buffer->current_offset + size) >= vulkan_globals.staging_buffer_size && !staging_buffer->submitted)
				R_SubmitStagingBuffer(engine->gl->sbuf->current_staging_buffer);

			staging_buffer = &engine->gl->sbuf->staging_buffers[engine->gl->sbuf->current_staging_buffer];
			R_FlushStagingCommandBuffer(staging_buffer);

			if (command_buffer)
				*command_buffer = staging_buffer->command_buffer;
			if (buffer)
				*buffer = staging_buffer->buffer;
			if (buffer_offset)
				*buffer_offset = staging_buffer->current_offset;

			unsigned char* data = staging_buffer->data + staging_buffer->current_offset;
			staging_buffer->current_offset += size;
			engine->gl->sbuf->num_stagings_in_flight += 1;

			return (byte*)data;
		}

		void R_StagingBeginCopy(void)
		{
			SDL_UnlockMutex(engine->gl->staging_mutex);
		}

		void R_StagingEndCopy(void)
		{
			SDL_LockMutex(engine->gl->staging_mutex);
			engine->gl->num_stagings_in_flight -= 1;
			SDL_CondBroadcast(engine->gl->staging_cond);
			SDL_UnlockMutex(engine->gl->staging_mutex);
		}


		void R_InitFanIndexBuffer()
		{
			VkResult	   err;
			VkDeviceMemory memory;
			const int	   bufferSize = sizeof(uint16_t) * FAN_INDEX_BUFFER_SIZE;

			ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
			buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_create_info.size = bufferSize;
			buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &vulkan_globals.fan_index_buffer);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateBuffer failed");

			engine->gl->SetObjectName((uint64_t)vulkan_globals.fan_index_buffer, VK_OBJECT_TYPE_BUFFER, "Quad Index Buffer");

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(vulkan_globals.device, vulkan_globals.fan_index_buffer, &memory_requirements);

			ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
			memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.allocationSize = memory_requirements.size;
			memory_allocate_info.memoryTypeIndex = engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

			Atomic_IncrementUInt32(&engine->gl->num_vulkan_dynbuf_allocations);
			Atomic_AddUInt64(&engine->gl->total_device_vulkan_allocation_size, memory_requirements.size);
			err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &memory);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateMemory failed");

			err = vkBindBufferMemory(vulkan_globals.device, vulkan_globals.fan_index_buffer, memory, 0);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");

			{
				VkBuffer		staging_buffer;
				VkCommandBuffer command_buffer;
				int				staging_offset;
				int				current_index = 0;
				int				i;
				uint16_t* staging_mem = (uint16_t*)R_StagingAllocate(bufferSize, 1, &command_buffer, &staging_buffer, &staging_offset);

				VkBufferCopy region;
				region.srcOffset = staging_offset;
				region.dstOffset = 0;
				region.size = bufferSize;
				vkCmdCopyBuffer(command_buffer, staging_buffer, vulkan_globals.fan_index_buffer, 1, &region);

				R_StagingBeginCopy();
				for (i = 0; i < FAN_INDEX_BUFFER_SIZE / 3; ++i)
				{
					staging_mem[current_index++] = 0;
					staging_mem[current_index++] = 1 + i;
					staging_mem[current_index++] = 2 + i;
				}
				R_StagingEndCopy();
			}
		}

		VkDescriptorSet R_AllocateDescriptorSet(vulkan_desc_set_layout_t* layout)
		{
			ZEROED_STRUCT(VkDescriptorSetAllocateInfo, descriptor_set_allocate_info);
			descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
			descriptor_set_allocate_info.descriptorSetCount = 1;
			descriptor_set_allocate_info.pSetLayouts = &layout->handle;

			VkDescriptorSet handle;
			vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &handle);

			Atomic_AddUInt32(&engine->gl->num_vulkan_combined_image_samplers, layout->num_combined_image_samplers);
			Atomic_AddUInt32(&engine->gl->num_vulkan_ubos_dynamic, layout->num_ubos_dynamic);
			Atomic_AddUInt32(&engine->gl->num_vulkan_ubos, layout->num_ubos);
			Atomic_AddUInt32(&engine->gl->num_vulkan_storage_buffers, layout->num_storage_buffers);
			Atomic_AddUInt32(&engine->gl->num_vulkan_input_attachments, layout->num_input_attachments);
			Atomic_AddUInt32(&engine->gl->num_vulkan_storage_images, layout->num_storage_images);
			Atomic_AddUInt32(&engine->gl->num_vulkan_sampled_images, layout->num_sampled_images);
			Atomic_AddUInt32(&engine->gl->num_acceleration_structures, layout->num_acceleration_structures);


			return handle;
		}

		void R_InitSamplers()
		{
			//engine->gl->WaitForDeviceIdle();
			SDL_Log("Initializing samplers\n");

			VkResult err;

			if (vulkan_globals.point_sampler == VK_NULL_HANDLE)
			{
				ZEROED_STRUCT(VkSamplerCreateInfo, sampler_create_info);
				sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				sampler_create_info.magFilter = VK_FILTER_NEAREST;
				sampler_create_info.minFilter = VK_FILTER_NEAREST;
				sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sampler_create_info.mipLodBias = 0.0f;
				sampler_create_info.maxAnisotropy = 1.0f;
				sampler_create_info.minLod = 0;
				sampler_create_info.maxLod = FLT_MAX;

				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.point_sampler, VK_OBJECT_TYPE_SAMPLER, "point");

				sampler_create_info.anisotropyEnable = VK_TRUE;
				sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.point_aniso_sampler, VK_OBJECT_TYPE_SAMPLER, "point_aniso");

				sampler_create_info.magFilter = VK_FILTER_LINEAR;
				sampler_create_info.minFilter = VK_FILTER_LINEAR;
				sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				sampler_create_info.anisotropyEnable = VK_FALSE;
				sampler_create_info.maxAnisotropy = 1.0f;

				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.linear_sampler, VK_OBJECT_TYPE_SAMPLER, "linear");

				sampler_create_info.anisotropyEnable = VK_TRUE;
				sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.linear_aniso_sampler, VK_OBJECT_TYPE_SAMPLER, "linear_aniso");
			}

			if (vulkan_globals.point_sampler_lod_bias != VK_NULL_HANDLE)
			{
				vkDestroySampler(vulkan_globals.device, vulkan_globals.point_sampler_lod_bias, NULL);
				vkDestroySampler(vulkan_globals.device, vulkan_globals.point_aniso_sampler_lod_bias, NULL);
				vkDestroySampler(vulkan_globals.device, vulkan_globals.linear_sampler_lod_bias, NULL);
				vkDestroySampler(vulkan_globals.device, vulkan_globals.linear_aniso_sampler_lod_bias, NULL);
			}

			{
				float lod_bias = 0.0f;
				if (engine->r_lodbias.value)
				{
					if (vulkan_globals.supersampling)
					{
						switch (vulkan_globals.sample_count)
						{
						case VK_SAMPLE_COUNT_2_BIT:
							lod_bias -= 0.5f;
							break;
						case VK_SAMPLE_COUNT_4_BIT:
							lod_bias -= 1.0f;
							break;
						case VK_SAMPLE_COUNT_8_BIT:
							lod_bias -= 1.5f;
							break;
						case VK_SAMPLE_COUNT_16_BIT:
							lod_bias -= 2.0f;
							break;
						default: /* silences gcc's -Wswitch */
							break;
						}
					}

					if (engine->r_scale.value >= 8)
						lod_bias += 3.0f;
					else if (engine->r_scale.value >= 4)
						lod_bias += 2.0f;
					else if (engine->r_scale.value >= 2)
						lod_bias += 1.0f;
				}

				lod_bias += engine->gl_lodbias.value;

				SDL_Log("Texture lod bias: %f\n", lod_bias);

				ZEROED_STRUCT(VkSamplerCreateInfo, sampler_create_info);
				sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				sampler_create_info.magFilter = VK_FILTER_NEAREST;
				sampler_create_info.minFilter = VK_FILTER_NEAREST;
				sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sampler_create_info.mipLodBias = lod_bias;
				sampler_create_info.maxAnisotropy = 1.0f;
				sampler_create_info.minLod = 0;
				sampler_create_info.maxLod = FLT_MAX;

				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler_lod_bias);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.point_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "point_lod_bias");

				sampler_create_info.anisotropyEnable = VK_TRUE;
				sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler_lod_bias);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.point_aniso_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "point_aniso_lod_bias");

				sampler_create_info.magFilter = VK_FILTER_LINEAR;
				sampler_create_info.minFilter = VK_FILTER_LINEAR;
				sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				sampler_create_info.anisotropyEnable = VK_FALSE;
				sampler_create_info.maxAnisotropy = 1.0f;

				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler_lod_bias);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.linear_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "linear_lod_bias");

				sampler_create_info.anisotropyEnable = VK_TRUE;
				sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
				err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler_lod_bias);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

				engine->gl->SetObjectName((uint64_t)vulkan_globals.linear_aniso_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "linear_aniso_lod_bias");
			}

			TexMgr_UpdateTextureDescriptorSets();
		}

		void TexMgr_SetFilterModes(gltexture_t* glt)
		{
			ZEROED_STRUCT(VkDescriptorImageInfo, image_info);
			image_info.imageView = glt->image_view;
			image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			bool enable_anisotropy = engine->vid_anisotropic.value && !(glt->flags & TEXPREF_NOPICMIP);

			VkSampler point_sampler = enable_anisotropy ? vulkan_globals.point_aniso_sampler_lod_bias : vulkan_globals.point_sampler_lod_bias;
			VkSampler linear_sampler = enable_anisotropy ? vulkan_globals.linear_aniso_sampler_lod_bias : vulkan_globals.linear_sampler_lod_bias;

			if (glt->flags & TEXPREF_NEAREST)
				image_info.sampler = point_sampler;
			else if (glt->flags & TEXPREF_LINEAR)
				image_info.sampler = linear_sampler;
			else
				image_info.sampler = (engine->vid_filter.value == 1) ? point_sampler : linear_sampler;

			ZEROED_STRUCT(VkWriteDescriptorSet, texture_write);
			texture_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			texture_write.dstSet = glt->descriptor_set;
			texture_write.dstBinding = 0;
			texture_write.dstArrayElement = 0;
			texture_write.descriptorCount = 1;
			texture_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			texture_write.pImageInfo = &image_info;

			vkUpdateDescriptorSets(vulkan_globals.device, 1, &texture_write, 0, NULL);
		}


		void TexMgr_UpdateTextureDescriptorSets(void)
		{
			gltexture_t* glt;

			for (glt = engine->active_gltextures; glt; glt = glt->next)
				TexMgr_SetFilterModes(glt);
		}

		void R_InitDynamicUniformBuffers(void)
		{
			R_InitDynamicBuffers(
				engine->gl->dyn_uniform_buffers, &engine->gl->dyn_uniform_buffer_memory, &engine->gl->current_dyn_uniform_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false, "uniform buffer");

			ZEROED_STRUCT(VkDescriptorSetAllocateInfo, descriptor_set_allocate_info);
			descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
			descriptor_set_allocate_info.descriptorSetCount = 1;
			descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.ubo_set_layout.handle;

			engine->gl->ubo_descriptor_sets[0] = R_AllocateDescriptorSet(&vulkan_globals.ubo_set_layout);
			engine->gl->ubo_descriptor_sets[1] = R_AllocateDescriptorSet(&vulkan_globals.ubo_set_layout);

			ZEROED_STRUCT(VkDescriptorBufferInfo, buffer_info);
			buffer_info.offset = 0;
			buffer_info.range = MAX_UNIFORM_ALLOC;

			ZEROED_STRUCT(VkWriteDescriptorSet, ubo_write);
			ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ubo_write.dstBinding = 0;
			ubo_write.dstArrayElement = 0;
			ubo_write.descriptorCount = 1;
			ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			ubo_write.pBufferInfo = &buffer_info;

			for (int i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
			{
				buffer_info.buffer = engine->gl->dyn_uniform_buffers[i].buffer;
				ubo_write.dstSet = engine->gl->ubo_descriptor_sets[i];
				vkUpdateDescriptorSets(vulkan_globals.device, 1, &ubo_write, 0, NULL);
			}
		}


		void R_InitDynamicIndexBuffers(void)
		{
			R_InitDynamicBuffers(engine->gl->dyn_index_buffers, &engine->gl->dyn_index_buffer_memory, &engine->gl->current_dyn_index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, false, "index buffer");
		}


		void R_InitDynamicVertexBuffers(void)
		{
			R_InitDynamicBuffers(
				engine->gl->dyn_vertex_buffers, &engine->gl->dyn_vertex_buffer_memory, &engine->gl->current_dyn_vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false, "vertex buffer");
		}

		void R_AllocateVulkanMemory(vulkan_memory_t* memory, VkMemoryAllocateInfo* memory_allocate_info, vulkan_memory_type_t type, a::atomic_uint32_t* num_allocations)
		{
			memory->type = type;
			if (memory->type != VULKAN_MEMORY_TYPE_NONE)
			{
				VkResult err = vkAllocateMemory(vulkan_globals.device, memory_allocate_info, NULL, &memory->handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateMemory failed");
				if (num_allocations)
					Atomic_IncrementUInt32(num_allocations);
			}
			memory->size = memory_allocate_info->allocationSize;
			if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
				Atomic_AddUInt64(&engine->gl->total_device_vulkan_allocation_size, memory->size);
			else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
				Atomic_AddUInt64(&engine->gl->total_host_vulkan_allocation_size, memory->size);
		}

		void R_InitDynamicBuffers(
			dynbuffer_t* buffers, vulkan_memory_t* memory, uint32_t* current_size, VkBufferUsageFlags usage_flags, bool get_device_address, const char* name)
		{
			int i;

			SDL_Log("Reallocating dynamic %ss (%u KB)\n", name, *current_size / 1024);

			VkResult err;

			if (get_device_address)
				usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;

			ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
			buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_create_info.size = *current_size;
			buffer_create_info.usage = usage_flags;

			for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
			{
				buffers[i].current_offset = 0;

				err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &buffers[i].buffer);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateBuffer failed");

				engine->gl->SetObjectName((uint64_t)buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, name);
			}

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(vulkan_globals.device, buffers[0].buffer, &memory_requirements);

			const size_t aligned_size = memory_requirements.size + memory_requirements.alignment;

			ZEROED_STRUCT(VkMemoryAllocateFlagsInfo, memory_allocate_flags_info);
			if (get_device_address)
			{
				memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
				memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
			}

			ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
			memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.pNext = get_device_address ? &memory_allocate_flags_info : NULL;
			memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
			memory_allocate_info.memoryTypeIndex =
				engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

			R_AllocateVulkanMemory(memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &engine->gl->num_vulkan_dynbuf_allocations);
			engine->gl->SetObjectName((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, name);

			for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
			{
				err = vkBindBufferMemory(vulkan_globals.device, buffers[i].buffer, memory->handle, i * aligned_size);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");
			}

			void* data;
			err = vkMapMemory(vulkan_globals.device, memory->handle, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");

			for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
			{
				buffers[i].data = (unsigned char*)data + (i * aligned_size);

				if (get_device_address)
				{
					VkBufferDeviceAddressInfoKHR buffer_device_address_info;
					memset(&buffer_device_address_info, 0, sizeof(buffer_device_address_info));
					buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
					buffer_device_address_info.buffer = buffers[i].buffer;
					VkDeviceAddress device_address = vulkan_globals.vk_get_buffer_device_address(vulkan_globals.device, &buffer_device_address_info);
					buffers[i].device_address = device_address;
				}
			}
		}

		void R_CreatePipelineLayouts()
		{
			SDL_Log("Creating pipeline layouts\n");

			VkResult err;

			{
				// Basic
				VkDescriptorSetLayout basic_descriptor_set_layouts[1] = { vulkan_globals.single_texture_set_layout.handle };

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 21 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = basic_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.basic_pipeline_layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.basic_pipeline_layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "basic_pipeline_layout");
				vulkan_globals.basic_pipeline_layout.push_constant_range = push_constant_range;
			}

			{
				// World
				VkDescriptorSetLayout world_descriptor_set_layouts[3] = {
					vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle };

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 21 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 3;
				pipeline_layout_create_info.pSetLayouts = world_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.world_pipeline_layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.world_pipeline_layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "world_pipeline_layout");
				vulkan_globals.world_pipeline_layout.push_constant_range = push_constant_range;
			}

			{
				// Alias
				VkDescriptorSetLayout alias_descriptor_set_layouts[3] = {
					vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.ubo_set_layout.handle };

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 21 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 3;
				pipeline_layout_create_info.pSetLayouts = alias_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.alias_pipelines[0].layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.alias_pipelines[0].layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "alias_pipeline_layout");
				vulkan_globals.alias_pipelines[0].layout.push_constant_range = push_constant_range;
			}

			{
				// MD5
				VkDescriptorSetLayout md5_descriptor_set_layouts[4] = {
					vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.ubo_set_layout.handle,
					vulkan_globals.joints_buffer_set_layout.handle };

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 21 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 4;
				pipeline_layout_create_info.pSetLayouts = md5_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.md5_pipelines[0].layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.md5_pipelines[0].layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "md5_pipeline_layout");
				vulkan_globals.md5_pipelines[0].layout.push_constant_range = push_constant_range;
			}

			{
				// Sky
				VkDescriptorSetLayout sky_layer_descriptor_set_layouts[2] = {
					vulkan_globals.single_texture_set_layout.handle,
					vulkan_globals.single_texture_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 23 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = sky_layer_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_pipeline_layout[0].handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.sky_pipeline_layout[0].handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "sky_pipeline_layout");
				vulkan_globals.sky_pipeline_layout[0].push_constant_range = push_constant_range;

				push_constant_range.size = 25 * sizeof(float);
				pipeline_layout_create_info.setLayoutCount = 2;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_pipeline_layout[1].handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.sky_pipeline_layout[1].handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "sky_layer_pipeline_layout");
				vulkan_globals.sky_pipeline_layout[1].push_constant_range = push_constant_range;
			}

			{
				// Postprocess
				VkDescriptorSetLayout postprocess_descriptor_set_layouts[1] = {
					vulkan_globals.input_attachment_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 2 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = postprocess_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.postprocess_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.postprocess_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "postprocess_pipeline_layout");
				vulkan_globals.postprocess_pipeline.layout.push_constant_range = push_constant_range;
			}

			{
				// Screen effects
				VkDescriptorSetLayout screen_effects_descriptor_set_layouts[1] = {
					vulkan_globals.screen_effects_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 3 * sizeof(uint32_t) + 8 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = screen_effects_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.screen_effects_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.screen_effects_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "screen_effects_pipeline_layout");
				vulkan_globals.screen_effects_pipeline.layout.push_constant_range = push_constant_range;
				vulkan_globals.screen_effects_scale_pipeline.layout.handle = vulkan_globals.screen_effects_pipeline.layout.handle;
				vulkan_globals.screen_effects_scale_pipeline.layout.push_constant_range = push_constant_range;
				vulkan_globals.screen_effects_scale_sops_pipeline.layout.handle = vulkan_globals.screen_effects_pipeline.layout.handle;
				vulkan_globals.screen_effects_scale_sops_pipeline.layout.push_constant_range = push_constant_range;
			}

			{
				// Texture warp
				VkDescriptorSetLayout tex_warp_descriptor_set_layouts[2] = {
					vulkan_globals.single_texture_set_layout.handle,
					vulkan_globals.single_texture_cs_write_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 1 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 2;
				pipeline_layout_create_info.pSetLayouts = tex_warp_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.cs_tex_warp_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.cs_tex_warp_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "cs_tex_warp_pipeline_layout");
				vulkan_globals.cs_tex_warp_pipeline.layout.push_constant_range = push_constant_range;
			}

			{
				// Show triangles
				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 0;
				pipeline_layout_create_info.pushConstantRangeCount = 0;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.showtris_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.showtris_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "showtris_pipeline_layout");
				vulkan_globals.showtris_pipeline.layout.push_constant_range = push_constant_range;
			}

			{
				// Update lightmaps
				VkDescriptorSetLayout update_lightmap_descriptor_set_layouts[1] = {
					vulkan_globals.lightmap_compute_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 6 * sizeof(uint32_t);
				push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = update_lightmap_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.update_lightmap_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.update_lightmap_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "update_lightmap_pipeline_layout");
				vulkan_globals.update_lightmap_pipeline.layout.push_constant_range = push_constant_range;
			}

			if (vulkan_globals.ray_query)
			{
				// Update lightmaps RT
				VkDescriptorSetLayout update_lightmap_rt_descriptor_set_layouts[1] = {
					vulkan_globals.lightmap_compute_rt_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 6 * sizeof(uint32_t);
				push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = update_lightmap_rt_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.update_lightmap_rt_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName(
					(uint64_t)vulkan_globals.update_lightmap_rt_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "update_lightmap_rt_pipeline_layout");
				vulkan_globals.update_lightmap_rt_pipeline.layout.push_constant_range = push_constant_range;
			}

			{
				// Indirect draw
				VkDescriptorSetLayout indirect_draw_descriptor_set_layouts[1] = {
					vulkan_globals.indirect_compute_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 6 * sizeof(uint32_t);
				push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = indirect_draw_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.indirect_draw_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.indirect_draw_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "indirect_draw_pipeline_layout");
				vulkan_globals.indirect_draw_pipeline.layout.push_constant_range = push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.indirect_clear_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.indirect_clear_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "indirect_clear_pipeline_layout");
				vulkan_globals.indirect_clear_pipeline.layout.push_constant_range = push_constant_range;
			}

#if defined(_DEBUG)
			if (vulkan_globals.ray_query)
			{
				// Ray debug
				VkDescriptorSetLayout ray_debug_descriptor_set_layouts[1] = {
					vulkan_globals.ray_debug_set_layout.handle,
				};

				ZEROED_STRUCT(VkPushConstantRange, push_constant_range);
				push_constant_range.offset = 0;
				push_constant_range.size = 15 * sizeof(float);
				push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

				ZEROED_STRUCT(VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
				pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipeline_layout_create_info.setLayoutCount = 1;
				pipeline_layout_create_info.pSetLayouts = ray_debug_descriptor_set_layouts;
				pipeline_layout_create_info.pushConstantRangeCount = 1;
				pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

				err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.ray_debug_pipeline.layout.handle);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout failed");
				engine->gl->SetObjectName((uint64_t)vulkan_globals.ray_debug_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "ray_debug_pipeline_layout");
				vulkan_globals.ray_debug_pipeline.layout.push_constant_range = push_constant_range;
			}
#endif
		}

	};
	class GL {
	public:
		Engine* engine;

		void SynchronizeEndRenderingTask(void)
		{
			if (prev_end_rendering_task != INVALID_TASK_HANDLE)
			{
				//Task_Join(prev_end_rendering_task, SDL_MUTEX_MAXWAIT);
				prev_end_rendering_task = INVALID_TASK_HANDLE;
			}
		}

		void WaitForDeviceIdle(void)
		{
			//assert(!Tasks_IsWorker());
			SynchronizeEndRenderingTask();
			if (!vulkan_globals.device_idle)
			{
				engine->ren->R_SubmitStagingBuffers();
				vkDeviceWaitIdle(vulkan_globals.device);
			}

			vulkan_globals.device_idle = true;
		}

		glheap_t* HeapCreate(VkDeviceSize segment_size, uint32_t page_size, uint32_t memory_type_index, vulkan_memory_type_t memory_type, const char* heap_name)
		{
			assert(Q_nextPow2(page_size) == page_size);
			assert(page_size >= (1 << (NUM_SMALL_ALLOC_SIZES + 1)));
			assert(segment_size >= page_size);
			assert((segment_size % page_size) == 0);
			assert((segment_size / page_size) <= MAX_PAGES);
			glheap_t* heap = (glheap_t*)Mem_Alloc(sizeof(glheap_t));
			heap->segment_size = segment_size;
			heap->num_pages_per_segment = segment_size / page_size;
			heap->num_masks_per_segment = (heap->num_pages_per_segment + 63) / 64;
			heap->page_size = page_size;
			heap->min_small_alloc_size = heap->page_size / 64;
			heap->page_size_shift = Q_log2(page_size);
			heap->small_alloc_shift = Q_log2(page_size / (1 << NUM_SMALL_ALLOC_SIZES));
			heap->memory_type_index = memory_type_index;
			heap->memory_type = memory_type;
			heap->name = heap_name;
			return heap;
		}

		void SetObjectName(uint64_t object, VkObjectType object_type, const char* name)
		{
#ifdef _DEBUG
			if (fpSetDebugUtilsObjectNameEXT && name)
			{
				ZEROED_STRUCT(VkDebugUtilsObjectNameInfoEXT, nameInfo);
				nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
				nameInfo.objectType = object_type;
				nameInfo.objectHandle = object;
				nameInfo.pObjectName = name;
				fpSetDebugUtilsObjectNameEXT(vulkan_globals.device, &nameInfo);
			};
#endif
		}

		size_t R_CreateBuffers(
			const int num_buffers, buffer_create_info_t* create_infos, vulkan_memory_t* memory, const VkFlags mem_requirements_mask, const VkFlags mem_preferred_mask,
			a::atomic_uint32_t* num_allocations, const char* memory_name)
		{
			VkResult		   err;
			VkBufferUsageFlags usage_union = 0;

			bool get_device_address = false;
			for (int i = 0; i < num_buffers; ++i)
			{
				if (vulkan_globals.vk_get_buffer_device_address)
				{
					if (create_infos[i].address)
					{
						get_device_address = true;
						create_infos[i].usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
					}
				}
				usage_union |= create_infos[i].usage;
			}

			bool map_memory = false;
			size_t	 total_size = 0;
			for (int i = 0; i < num_buffers; ++i)
			{
				ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
				buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_create_info.size = create_infos[i].size;
				buffer_create_info.usage = create_infos[i].usage;
				err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, create_infos[i].buffer);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

				SetObjectName((uint64_t)*create_infos[i].buffer, VK_OBJECT_TYPE_BUFFER, va("%s buffer", create_infos[i].name));

				VkMemoryRequirements memory_requirements;
				vkGetBufferMemoryRequirements(vulkan_globals.device, *create_infos[i].buffer, &memory_requirements);
				const size_t alignment = max(memory_requirements.alignment, create_infos[i].alignment);
				total_size = (total_size + alignment);
				total_size += memory_requirements.size;
				map_memory = map_memory || create_infos[i].mapped;
			}

			ZEROED_STRUCT(VkMemoryAllocateFlagsInfo, memory_allocate_flags_info);
			if (get_device_address)
			{
				memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
				memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
			}

			uint32_t memory_type_bits = 0;
			{
				ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
				buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_create_info.size = total_size;
				buffer_create_info.usage = usage_union;
				VkBuffer dummy_buffer;
				err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &dummy_buffer);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateBuffer failed");
				VkMemoryRequirements memory_requirements;
				// Vulkan spec:
				// The memoryTypeBits member is identical for all VkBuffer objects created with the same value for the flags and usage members in the VkBufferCreateInfo
				// structure passed to vkCreateBuffer. Further, if usage1 and usage2 of type VkBufferUsageFlags are such that the bits set in usage2 are a subset of the
				// bits set in usage1, then the bits set in memoryTypeBits returned for usage1 must be a subset of the bits set in memoryTypeBits returned for usage2,
				// for all values of flags.
				vkGetBufferMemoryRequirements(vulkan_globals.device, dummy_buffer, &memory_requirements);
				memory_type_bits = memory_requirements.memoryTypeBits;
				vkDestroyBuffer(vulkan_globals.device, dummy_buffer, NULL);
			}

			ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
			memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.pNext = get_device_address ? &memory_allocate_flags_info : NULL;
			memory_allocate_info.allocationSize = total_size;
			memory_allocate_info.memoryTypeIndex = engine->gl->MemoryTypeFromProperties(memory_type_bits, mem_requirements_mask, mem_preferred_mask);

			engine->ren->R_AllocateVulkanMemory(memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, num_allocations);
			SetObjectName((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, memory_name);

			byte* mapped_base = NULL;
			if (map_memory)
			{
				err = vkMapMemory(vulkan_globals.device, memory->handle, 0, total_size, 0, (void**)&mapped_base);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");
			}

			size_t current_offset = 0;
			for (int i = 0; i < num_buffers; ++i)
			{
				VkMemoryRequirements memory_requirements;
				vkGetBufferMemoryRequirements(vulkan_globals.device, *create_infos[i].buffer, &memory_requirements);
				const size_t alignment = max(memory_requirements.alignment, create_infos[i].alignment);
				current_offset = (current_offset + alignment);

				err = vkBindBufferMemory(vulkan_globals.device, *create_infos[i].buffer, memory->handle, current_offset);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");

				if (create_infos[i].mapped)
					*create_infos[i].mapped = mapped_base + current_offset;

				current_offset += memory_requirements.size;

				if (get_device_address && create_infos[i].address)
				{
					VkBufferDeviceAddressInfoKHR buffer_device_address_info;
					memset(&buffer_device_address_info, 0, sizeof(buffer_device_address_info));
					buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
					buffer_device_address_info.buffer = *create_infos[i].buffer;
					*create_infos[i].address = vulkan_globals.vk_get_buffer_device_address(vulkan_globals.device, &buffer_device_address_info);
				}
			}

			return total_size;
		}


		void R_CreatePaletteOctreeBuffers(uint32_t* colors, int num_colors, palette_octree_node_t* nodes, int num_nodes)
		{
			const int colors_size = num_colors * sizeof(uint32_t);
			const int nodes_size = num_nodes * sizeof(palette_octree_node_t);

			buffer_create_info_t buffer_create_infos[2] = {
				{&palette_colors_buffer, colors_size, 0, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL, NULL, "Palette colors"},
				{&palette_octree_buffer, nodes_size, 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL, NULL, "Palette octree"},
			};

			vulkan_memory_t memory;
			R_CreateBuffers(
				countof(buffer_create_infos), buffer_create_infos, &memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &num_vulkan_misc_allocations, "Palette");

			{
				VkBuffer		staging_buffer;
				VkCommandBuffer command_buffer;
				int				staging_offset;
				uint32_t* staging_memory = (uint32_t*)engine->ren->R_StagingAllocate(colors_size, 1, &command_buffer, &staging_buffer, &staging_offset);

				VkBufferCopy region;
				region.srcOffset = staging_offset;
				region.dstOffset = 0;
				region.size = colors_size;
				vkCmdCopyBuffer(command_buffer, staging_buffer, palette_colors_buffer, 1, &region);

				engine->ren->R_StagingBeginCopy();
				memcpy(staging_memory, colors, colors_size);
				engine->ren->R_StagingEndCopy();

				ZEROED_STRUCT(VkBufferViewCreateInfo, buffer_view_create_info);
				buffer_view_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
				buffer_view_create_info.buffer = palette_colors_buffer;
				buffer_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
				buffer_view_create_info.range = VK_WHOLE_SIZE;
				VkResult err = vkCreateBufferView(vulkan_globals.device, &buffer_view_create_info, NULL, &palette_buffer_view);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateBufferView failed");
				SetObjectName((uint64_t)palette_buffer_view, VK_OBJECT_TYPE_BUFFER_VIEW, "Palette colors");
			}

			{
				VkBuffer		staging_buffer;
				VkCommandBuffer command_buffer;
				int				staging_offset;
				uint32_t* staging_memory = (uint32_t*)engine->ren->R_StagingAllocate(nodes_size, 1, &command_buffer, &staging_buffer, &staging_offset);

				VkBufferCopy region;
				region.srcOffset = staging_offset;
				region.dstOffset = 0;
				region.size = nodes_size;
				vkCmdCopyBuffer(command_buffer, staging_buffer, palette_octree_buffer, 1, &region);

				engine->ren->R_StagingBeginCopy();
				memcpy(staging_memory, nodes, nodes_size);
				engine->ren->R_StagingEndCopy();
			}
		}


		task_handle_t prev_end_rendering_task = INVALID_TASK_HANDLE;

		uint32_t		   current_dyn_vertex_buffer_size = INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024;
		uint32_t		   current_dyn_index_buffer_size = INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024;
		uint32_t		   current_dyn_uniform_buffer_size = INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024;
		uint32_t		   current_dyn_storage_buffer_size = 0; // Only used for RT so allocate lazily
		vulkan_memory_t dyn_vertex_buffer_memory;
		vulkan_memory_t dyn_index_buffer_memory;
		vulkan_memory_t dyn_uniform_buffer_memory;
		vulkan_memory_t dyn_storage_buffer_memory;
		vulkan_memory_t lights_buffer_memory;
		dynbuffer_t	   dyn_vertex_buffers[NUM_DYNAMIC_BUFFERS];
		dynbuffer_t	   dyn_index_buffers[NUM_DYNAMIC_BUFFERS];
		dynbuffer_t	   dyn_uniform_buffers[NUM_DYNAMIC_BUFFERS];
		dynbuffer_t	   dyn_storage_buffers[NUM_DYNAMIC_BUFFERS];
		int			   current_dyn_buffer_index = 0;
		VkDescriptorSet ubo_descriptor_sets[2];

		int				current_garbage_index = 0;
		int				num_device_memory_garbage[GARBAGE_FRAME_COUNT];
		int				num_buffer_garbage[GARBAGE_FRAME_COUNT];
		int				num_desc_set_garbage[GARBAGE_FRAME_COUNT];
		vulkan_memory_t* device_memory_garbage[GARBAGE_FRAME_COUNT];
		VkDescriptorSet* descriptor_set_garbage[GARBAGE_FRAME_COUNT];
		VkBuffer* buffer_garbage[GARBAGE_FRAME_COUNT];


		VkInstance				vulkan_instance;
		VkPhysicalDevice			vulkan_physical_device;
		VkSurfaceKHR				vulkan_surface;
		VkSurfaceCapabilitiesKHR vulkan_surface_capabilities;
		VkSwapchainKHR			vulkan_swapchain;

		uint32_t			num_swap_chain_images;
		bool			render_resources_created = false;
		uint32_t			current_cb_index;
		VkCommandPool	primary_command_pools[PCBX_NUM];
		VkCommandPool* secondary_command_pools[SCBX_NUM];
		VkCommandPool	transient_command_pool;
		VkCommandBuffer	primary_command_buffers[PCBX_NUM][DOUBLE_BUFFERED];
		VkCommandBuffer* secondary_command_buffers[SCBX_NUM][DOUBLE_BUFFERED];
		VkFence			command_buffer_fences[DOUBLE_BUFFERED];
		bool			frame_submitted[DOUBLE_BUFFERED];
		VkFramebuffer	main_framebuffers[NUM_COLOR_BUFFERS];
		VkSemaphore		image_aquired_semaphores[DOUBLE_BUFFERED];
		VkSemaphore		draw_complete_semaphores[DOUBLE_BUFFERED];
		VkFramebuffer	ui_framebuffers[MAX_SWAP_CHAIN_IMAGES];
		VkImage			swapchain_images[MAX_SWAP_CHAIN_IMAGES];
		VkImageView		swapchain_images_views[MAX_SWAP_CHAIN_IMAGES];
		VkImage			depth_buffer;
		vulkan_memory_t	depth_buffer_memory;
		VkImageView		depth_buffer_view;
		vulkan_memory_t	color_buffers_memory[NUM_COLOR_BUFFERS];
		VkImageView		color_buffers_view[NUM_COLOR_BUFFERS];
		VkImage			msaa_color_buffer;
		vulkan_memory_t	msaa_color_buffer_memory;
		VkImageView		msaa_color_buffer_view;
		VkDescriptorSet	postprocess_descriptor_set;
		VkBuffer			palette_colors_buffer;
		VkBufferView		palette_buffer_view;
		VkBuffer			palette_octree_buffer;

		PFN_vkGetInstanceProcAddr					  fpGetInstanceProcAddr;
		PFN_vkGetDeviceProcAddr						  fpGetDeviceProcAddr;
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR		  fpGetPhysicalDeviceSurfaceSupportKHR;
		PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR  fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
		PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR fpGetPhysicalDeviceSurfaceCapabilities2KHR;
		PFN_vkGetPhysicalDeviceSurfaceFormatsKHR		  fpGetPhysicalDeviceSurfaceFormatsKHR;
		PFN_vkGetPhysicalDeviceSurfacePresentModesKHR  fpGetPhysicalDeviceSurfacePresentModesKHR;
		PFN_vkCreateSwapchainKHR						  fpCreateSwapchainKHR;
		PFN_vkDestroySwapchainKHR					  fpDestroySwapchainKHR;
		PFN_vkGetSwapchainImagesKHR					  fpGetSwapchainImagesKHR;
		PFN_vkAcquireNextImageKHR					  fpAcquireNextImageKHR;
		PFN_vkQueuePresentKHR						  fpQueuePresentKHR;
		PFN_vkEnumerateInstanceVersion				  fpEnumerateInstanceVersion;
		PFN_vkGetPhysicalDeviceFeatures2				  fpGetPhysicalDeviceFeatures2;
		PFN_vkGetPhysicalDeviceProperties2			  fpGetPhysicalDeviceProperties2;

		VkCommandPool   staging_command_pool;
		vulkan_memory_t staging_memory;
		int			   current_staging_buffer = 0;
		int			   num_stagings_in_flight = 0;
		SDL_mutex* staging_mutex;
		SDL_cond* staging_cond;

		a::atomic_uint32_t num_vulkan_tex_allocations;
		a::atomic_uint32_t num_vulkan_bmodel_allocations;
		a::atomic_uint32_t num_vulkan_mesh_allocations;
		a::atomic_uint32_t num_vulkan_misc_allocations;
		a::atomic_uint32_t num_vulkan_dynbuf_allocations;
		a::atomic_uint32_t num_vulkan_combined_image_samplers;
		a::atomic_uint32_t num_vulkan_ubos_dynamic;
		a::atomic_uint32_t num_vulkan_ubos;
		a::atomic_uint32_t num_vulkan_storage_buffers;
		a::atomic_uint32_t num_vulkan_input_attachments;
		a::atomic_uint32_t num_vulkan_storage_images;
		a::atomic_uint32_t num_vulkan_sampled_images;
		a::atomic_uint32_t num_acceleration_structures;
		a::atomic_uint64_t total_device_vulkan_allocation_size;
		a::atomic_uint64_t total_host_vulkan_allocation_size;

		bool use_simd;

		SDL_mutex* vertex_allocate_mutex;
		SDL_mutex* index_allocate_mutex;
		SDL_mutex* uniform_allocate_mutex;
		SDL_mutex* storage_allocate_mutex;
		SDL_mutex* garbage_mutex;


#ifdef _DEBUG
		PFN_vkCreateDebugUtilsMessengerEXT fpCreateDebugUtilsMessengerEXT;
		PFN_vkSetDebugUtilsObjectNameEXT		  fpSetDebugUtilsObjectNameEXT;

		VkDebugUtilsMessengerEXT debug_utils_messenger;

#endif

		int MemoryTypeFromProperties(uint32_t type_bits, VkFlags requirements_mask, VkFlags preferred_mask)
		{
			uint32_t current_type_bits = type_bits;
			uint32_t i;

			for (i = 0; i < VK_MAX_MEMORY_TYPES; i++)
			{
				if ((current_type_bits & 1) == 1)
				{
					if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & (requirements_mask | preferred_mask)) == (requirements_mask | preferred_mask))
						return i;
				}
				current_type_bits >>= 1;
			}

			current_type_bits = type_bits;
			for (i = 0; i < VK_MAX_MEMORY_TYPES; i++)
			{
				if ((current_type_bits & 1) == 1)
				{
					if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask)
						return i;
				}
				current_type_bits >>= 1;
			}

			SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Could not find memory type");
			return 0;
		}


		class Instance {
		public:
			Engine* engine;


			Instance(Engine e) {
				engine = &e;
				engine->gl->instance = this;

				// Initialize Vulkan instance

				VkResult	 err;
				uint32_t	 i;
				unsigned int sdl_extension_count;
				vulkan_globals.debug_utils = false;

				if (!SDL_Vulkan_GetInstanceExtensions(engine->vid->draw_context, &sdl_extension_count, NULL))
					SDL_Log("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

				const char** const instance_extensions = (const char** const)Mem_Alloc(sizeof(const char*) * (sdl_extension_count + 3));
				if (!SDL_Vulkan_GetInstanceExtensions(engine->vid->draw_context, &sdl_extension_count, instance_extensions))
					SDL_Log("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

				uint32_t instance_extension_count;
				err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);

				uint32_t additionalExtensionCount = 0;

				vulkan_globals.get_surface_capabilities_2 = false;
				vulkan_globals.get_physical_device_properties_2 = false;
				if (err == VK_SUCCESS || instance_extension_count > 0)
				{
					VkExtensionProperties* extension_props = (VkExtensionProperties*)Mem_Alloc(sizeof(VkExtensionProperties) * instance_extension_count);
					err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, extension_props);

					for (i = 0; i < instance_extension_count; ++i)
					{
						if (strcmp(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
							vulkan_globals.get_surface_capabilities_2 = true;
						if (strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
							vulkan_globals.get_physical_device_properties_2 = true;
#if _DEBUG
						if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension_props[i].extensionName) == 0)
							vulkan_globals.debug_utils = true;
#endif
					}

					Mem_Free(extension_props);
				}

				vulkan_globals.vulkan_1_1_available = false;
				engine->gl->fpGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
				GET_INSTANCE_PROC_ADDR(EnumerateInstanceVersion);
				if (engine->gl->fpEnumerateInstanceVersion)
				{
					uint32_t api_version = 0;
					engine->gl->fpEnumerateInstanceVersion(&api_version);
					if (api_version >= VK_MAKE_VERSION(1, 1, 0))
					{
						SDL_Log("Using Vulkan 1.1\n");
						vulkan_globals.vulkan_1_1_available = true;
					}
				}

				ZEROED_STRUCT(VkApplicationInfo, application_info);
				application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				application_info.pApplicationName = "Tremor";
				application_info.applicationVersion = 1;
				application_info.pEngineName = "Tremor";
				application_info.engineVersion = 1;
				application_info.apiVersion = vulkan_globals.vulkan_1_1_available ? VK_MAKE_VERSION(1, 1, 0) : VK_MAKE_VERSION(1, 0, 0);

				ZEROED_STRUCT(VkInstanceCreateInfo, instance_create_info);
				instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				instance_create_info.pApplicationInfo = &application_info;
				instance_create_info.ppEnabledExtensionNames = instance_extensions;

				if (vulkan_globals.get_surface_capabilities_2)
					instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
				if (vulkan_globals.get_physical_device_properties_2)
					instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

#ifdef _DEBUG
				if (vulkan_globals.debug_utils)
					instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

				const char* const layer_names[] = { "VK_LAYER_KHRONOS_validation" };
				if (vulkan_globals.validation)
				{
					SDL_Log("Using VK_LAYER_KHRONOS_validation\n");
					instance_create_info.enabledLayerCount = 1;
					instance_create_info.ppEnabledLayerNames = layer_names;
				}
#endif

				instance_create_info.enabledExtensionCount = sdl_extension_count + additionalExtensionCount;

				err = vkCreateInstance(&instance_create_info, NULL, &engine->gl->vulkan_instance);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Couldn't create Vulkan instance");

				if (!SDL_Vulkan_CreateSurface(engine->vid->draw_context, engine->gl->vulkan_instance, &engine->gl->vulkan_surface))
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Couldn't create Vulkan surface");

				GET_INSTANCE_PROC_ADDR(GetDeviceProcAddr);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceSupportKHR);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceCapabilitiesKHR);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceFormatsKHR);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfacePresentModesKHR);
				GET_INSTANCE_PROC_ADDR(GetSwapchainImagesKHR);

				if (vulkan_globals.get_physical_device_properties_2)
				{
					GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceProperties2);
					GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceFeatures2);
				}

				if (vulkan_globals.get_surface_capabilities_2)
					GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceCapabilities2KHR);

				SDL_Log("Instance extensions:\n");
				for (i = 0; i < (sdl_extension_count + additionalExtensionCount); ++i)
					SDL_Log(" %s\n", instance_extensions[i]);
				SDL_Log("\n");

#ifdef _DEBUG
				if (vulkan_globals.validation)
				{
					SDL_Log("Creating debug report callback\n");
					GET_INSTANCE_PROC_ADDR(CreateDebugUtilsMessengerEXT);
					if (engine->gl->fpCreateDebugUtilsMessengerEXT)
					{
						ZEROED_STRUCT(VkDebugUtilsMessengerCreateInfoEXT, debug_utils_messenger_create_info);
						debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
						debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
						debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
						debug_utils_messenger_create_info.pfnUserCallback = DebugMessageCallback;

						err = engine->gl->fpCreateDebugUtilsMessengerEXT(engine->gl->vulkan_instance, &debug_utils_messenger_create_info, NULL, &engine->gl->debug_utils_messenger);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Could not create debug report callback");
					}
				}
#endif

				Mem_Free((void*)instance_extensions);
			}

		};
		class CommandBuffers {
		public:
			Engine* engine;
			CommandBuffers(Engine e) {
				engine = &e;
				engine->gl->cbuf = this;

				SDL_Log("Creating command buffers\n");

				VkResult err;

				{
					ZEROED_STRUCT(VkCommandPoolCreateInfo, command_pool_create_info);
					command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
					command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
					command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
					err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->transient_command_pool);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateCommandPool failed");
				}

				ZEROED_STRUCT(VkCommandPoolCreateInfo, command_pool_create_info);
				command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

				for (int pcbx_index = 0; pcbx_index < PCBX_NUM; ++pcbx_index)
				{
					err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->primary_command_pools[pcbx_index]);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateCommandPool failed");

					ZEROED_STRUCT(VkCommandBufferAllocateInfo, command_buffer_allocate_info);
					command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
					command_buffer_allocate_info.commandPool = engine->gl->primary_command_pools[pcbx_index];
					command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;

					err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, engine->gl->primary_command_buffers[pcbx_index]);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateCommandBuffers failed");
					for (int i = 0; i < DOUBLE_BUFFERED; ++i)
						engine->gl->SetObjectName(
							(uint64_t)(uintptr_t)engine->gl->primary_command_buffers[pcbx_index][i], VK_OBJECT_TYPE_COMMAND_BUFFER, va("PCBX index: %d cb_index: %d", pcbx_index, i));
				}

				for (int scbx_index = 0; scbx_index < SCBX_NUM; ++scbx_index)
				{
					const int multiplicity = SECONDARY_CB_MULTIPLICITY[scbx_index];
					vulkan_globals.secondary_cb_contexts[scbx_index] = (cb_context_t*) Mem_Alloc(multiplicity * sizeof(cb_context_t));
					engine->gl->secondary_command_pools[scbx_index] = (VkCommandPool*)Mem_Alloc(multiplicity * sizeof(VkCommandPool));
					for (int i = 0; i < DOUBLE_BUFFERED; ++i)
						engine->gl->secondary_command_buffers[scbx_index][i] = (VkCommandBuffer*)Mem_Alloc(multiplicity * sizeof(VkCommandBuffer));
					for (int i = 0; i < multiplicity; ++i)
					{
						err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->secondary_command_pools[scbx_index][i]);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateCommandPool failed");

						ZEROED_STRUCT(VkCommandBufferAllocateInfo, command_buffer_allocate_info);
						command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
						command_buffer_allocate_info.commandPool = engine->gl->secondary_command_pools[scbx_index][i];
						command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;
						command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;

						VkCommandBuffer command_buffers[DOUBLE_BUFFERED];
						err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateCommandBuffers failed");
						for (int j = 0; j < DOUBLE_BUFFERED; ++j)
						{
							engine->gl->secondary_command_buffers[scbx_index][j][i] = command_buffers[j];
							engine->gl->SetObjectName(
								(uint64_t)(uintptr_t)command_buffers[j], VK_OBJECT_TYPE_COMMAND_BUFFER, va("SCBX index: %d sub_index: %d cb_index: %d", scbx_index, i, j));
						}
					}
				}

				ZEROED_STRUCT(VkFenceCreateInfo, fence_create_info);
				fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

				for (int i = 0; i < DOUBLE_BUFFERED; ++i)
				{
					err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &engine->gl->command_buffer_fences[i]);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateFence failed");

					ZEROED_STRUCT(VkSemaphoreCreateInfo, semaphore_create_info);
					semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
					err = vkCreateSemaphore(vulkan_globals.device, &semaphore_create_info, NULL, &engine->gl->draw_complete_semaphores[i]);
				}

				
			}
		};
		class Device {
		public:
			Engine* engine;
			Device(Engine e) {
				engine = &e;
				engine->gl->device = this;

				VkResult err;
				uint32_t i;
				int		 arg_index;
				int		 device_index = 0;

				bool subgroup_size_control = false;

				uint32_t physical_device_count;
				err = vkEnumeratePhysicalDevices(engine->gl->vulkan_instance, &physical_device_count, NULL);
				if (err != VK_SUCCESS || physical_device_count == 0)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find any Vulkan devices");

				arg_index = engine->com->CheckParm("-device");
				if (arg_index && (arg_index < (engine->argc - 1)))
				{
					const char* device_num = engine->argv[arg_index + 1];
					device_index = CLAMP(0, atoi(device_num) - 1, (int)physical_device_count - 1);
				}

				VkPhysicalDevice* physical_devices = (VkPhysicalDevice*)Mem_Alloc(sizeof(VkPhysicalDevice) * physical_device_count);
				vkEnumeratePhysicalDevices(engine->gl->vulkan_instance, &physical_device_count, physical_devices);
				if (!arg_index)
				{
					// If no device was specified by command line pick first discrete GPU
					for (i = 0; i < physical_device_count; ++i)
					{
						VkPhysicalDeviceProperties device_properties;
						vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
						if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
						{
							device_index = (int)i;
							break;
						}
					}
				}
				engine->gl->vulkan_physical_device = physical_devices[device_index];
				Mem_Free(physical_devices);

				bool found_swapchain_extension = false;
				vulkan_globals.dedicated_allocation = false;
				vulkan_globals.full_screen_exclusive = false;
				vulkan_globals.swap_chain_full_screen_acquired = false;
				vulkan_globals.screen_effects_sops = false;
				vulkan_globals.ray_query = false;

				vkGetPhysicalDeviceMemoryProperties(engine->gl->vulkan_physical_device, &vulkan_globals.memory_properties);
				vkGetPhysicalDeviceProperties(engine->gl->vulkan_physical_device, &vulkan_globals.device_properties);

				bool driver_properties_available = false;
				uint32_t device_extension_count;
				err = vkEnumerateDeviceExtensionProperties(engine->gl->vulkan_physical_device, NULL, &device_extension_count, NULL);

				if (err == VK_SUCCESS || device_extension_count > 0)
				{
					VkExtensionProperties* device_extensions = (VkExtensionProperties*)Mem_Alloc(sizeof(VkExtensionProperties) * device_extension_count);
					err = vkEnumerateDeviceExtensionProperties(engine->gl->vulkan_physical_device, NULL, &device_extension_count, device_extensions);

					for (i = 0; i < device_extension_count; ++i)
					{
						if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							found_swapchain_extension = true;
						if (strcmp(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							vulkan_globals.dedicated_allocation = true;
						if (vulkan_globals.get_physical_device_properties_2 && strcmp(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							driver_properties_available = true;
						if (strcmp(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							subgroup_size_control = true;
#if defined(VK_EXT_full_screen_exclusive)
						if (strcmp(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							vulkan_globals.full_screen_exclusive = true;
#endif
						if (strcmp(VK_KHR_RAY_QUERY_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							vulkan_globals.ray_query = true;
					}

					Mem_Free(device_extensions);
				}

				const char* vendor = NULL;
				ZEROED_STRUCT(VkPhysicalDeviceDriverProperties, driver_properties);
				if (driver_properties_available)
				{
					driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

					ZEROED_STRUCT(VkPhysicalDeviceProperties2, physical_device_properties_2);
					physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
					physical_device_properties_2.pNext = &driver_properties;
					engine->gl->fpGetPhysicalDeviceProperties2(engine->gl->vulkan_physical_device, &physical_device_properties_2);

					vendor = GetDeviceVendorFromDriverProperties(&driver_properties);
				}

				if (!vendor)
					vendor = GetDeviceVendorFromDeviceProperties();

				if (vendor)
					SDL_Log("Vendor: %s\n", vendor);
				else
					SDL_Log("Vendor: Unknown (0x%x)\n", vulkan_globals.device_properties.vendorID);

				SDL_Log("Device: %s\n", vulkan_globals.device_properties.deviceName);

				if (driver_properties_available)
					SDL_Log("Driver: %s %s\n", driver_properties.driverName, driver_properties.driverInfo);

				if (!found_swapchain_extension)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find %s extension", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

				bool found_graphics_queue = false;

				uint32_t vulkan_queue_count;
				vkGetPhysicalDeviceQueueFamilyProperties(engine->gl->vulkan_physical_device, &vulkan_queue_count, NULL);
				if (vulkan_queue_count == 0)
				{
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find any Vulkan queues");
				}

				VkQueueFamilyProperties* queue_family_properties = (VkQueueFamilyProperties*)Mem_Alloc(vulkan_queue_count * sizeof(VkQueueFamilyProperties));
				vkGetPhysicalDeviceQueueFamilyProperties(engine->gl->vulkan_physical_device, &vulkan_queue_count, queue_family_properties);

				// Iterate over each queue to learn whether it supports presenting:
				VkBool32* queue_supports_present = (VkBool32*)Mem_Alloc(vulkan_queue_count * sizeof(VkBool32));
				for (i = 0; i < vulkan_queue_count; ++i)
					engine->gl->fpGetPhysicalDeviceSurfaceSupportKHR(engine->gl->vulkan_physical_device, i, engine->gl->vulkan_surface, &queue_supports_present[i]);

				for (i = 0; i < vulkan_queue_count; ++i)
				{
					if (((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && queue_supports_present[i])
					{
						found_graphics_queue = true;
						vulkan_globals.gfx_queue_family_index = i;
						break;
					}
				}

				Mem_Free(queue_supports_present);
				Mem_Free(queue_family_properties);

				if (!found_graphics_queue)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find graphics queue");

				float queue_priorities[] = { 0.0 };
				ZEROED_STRUCT(VkDeviceQueueCreateInfo, queue_create_info);
				queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queue_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
				queue_create_info.queueCount = 1;
				queue_create_info.pQueuePriorities = queue_priorities;

				ZEROED_STRUCT(VkPhysicalDeviceSubgroupProperties, physical_device_subgroup_properties);
				ZEROED_STRUCT(VkPhysicalDeviceSubgroupSizeControlPropertiesEXT, physical_device_subgroup_size_control_properties);
				ZEROED_STRUCT(VkPhysicalDeviceSubgroupSizeControlFeaturesEXT, subgroup_size_control_features);
				ZEROED_STRUCT(VkPhysicalDeviceBufferDeviceAddressFeaturesKHR, buffer_device_address_features);
				ZEROED_STRUCT(VkPhysicalDeviceAccelerationStructureFeaturesKHR, acceleration_structure_features);
				ZEROED_STRUCT(VkPhysicalDeviceRayQueryFeaturesKHR, ray_query_features);
				memset(&vulkan_globals.physical_device_acceleration_structure_properties, 0, sizeof(vulkan_globals.physical_device_acceleration_structure_properties));
				if (vulkan_globals.vulkan_1_1_available)
				{
					ZEROED_STRUCT(VkPhysicalDeviceProperties2KHR, physical_device_properties_2);
					physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
					void** device_properties_next = &physical_device_properties_2.pNext;

					if (subgroup_size_control)
					{
						physical_device_subgroup_size_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
						CHAIN_PNEXT(device_properties_next, physical_device_subgroup_size_control_properties);
						physical_device_subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
						CHAIN_PNEXT(device_properties_next, physical_device_subgroup_properties);
					}
					if (vulkan_globals.ray_query)
					{
						vulkan_globals.physical_device_acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
						CHAIN_PNEXT(device_properties_next, vulkan_globals.physical_device_acceleration_structure_properties);
					}

					engine->gl->fpGetPhysicalDeviceProperties2(engine->gl->vulkan_physical_device, &physical_device_properties_2);

					ZEROED_STRUCT(VkPhysicalDeviceFeatures2, physical_device_features_2);
					physical_device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
					void** device_features_next = &physical_device_features_2.pNext;

					if (subgroup_size_control)
					{
						subgroup_size_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
						CHAIN_PNEXT(device_features_next, subgroup_size_control_features);
					}
					if (vulkan_globals.ray_query)
					{
						buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
						CHAIN_PNEXT(device_features_next, buffer_device_address_features);
						acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
						CHAIN_PNEXT(device_features_next, acceleration_structure_features);
						ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
						CHAIN_PNEXT(device_features_next, ray_query_features);
					}

					engine->gl->fpGetPhysicalDeviceFeatures2(engine->gl->vulkan_physical_device, &physical_device_features_2);
					vulkan_globals.device_features = physical_device_features_2.features;
				}
				else
					vkGetPhysicalDeviceFeatures(engine->gl->vulkan_physical_device, &vulkan_globals.device_features);

#ifdef __APPLE__ // MoltenVK lies about this
				vulkan_globals.device_features.sampleRateShading = false;
#endif

				vulkan_globals.screen_effects_sops =
					vulkan_globals.vulkan_1_1_available && subgroup_size_control && subgroup_size_control_features.subgroupSizeControl &&
					subgroup_size_control_features.computeFullSubgroups && ((physical_device_subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0) &&
					((physical_device_subgroup_properties.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0)
					// Shader only supports subgroup sizes from 4 to 64. 128 can't be supported because Vulkan spec states that workgroup size
					// in x dimension must be a multiple of the subgroup size for VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT.
					&& (physical_device_subgroup_size_control_properties.minSubgroupSize >= 4) && (physical_device_subgroup_size_control_properties.maxSubgroupSize <= 64);
				if (vulkan_globals.screen_effects_sops)
					SDL_Log("Using subgroup operations\n");

				vulkan_globals.ray_query = vulkan_globals.ray_query && acceleration_structure_features.accelerationStructure && ray_query_features.rayQuery &&
					buffer_device_address_features.bufferDeviceAddress;
				if (vulkan_globals.ray_query)
					SDL_Log("Using ray queries\n");

				const char* device_extensions[32] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
				uint32_t	numEnabledExtensions = 1;
				if (vulkan_globals.dedicated_allocation)
				{
					device_extensions[numEnabledExtensions++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
				}
				if (vulkan_globals.screen_effects_sops)
					device_extensions[numEnabledExtensions++] = VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME;
#if defined(VK_EXT_full_screen_exclusive)
				if (vulkan_globals.full_screen_exclusive)
					device_extensions[numEnabledExtensions++] = VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME;
#endif
				if (vulkan_globals.ray_query)
				{
					device_extensions[numEnabledExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
				}

				const VkBool32 extended_format_support = vulkan_globals.device_features.shaderStorageImageExtendedFormats;
				const VkBool32 sampler_anisotropic = vulkan_globals.device_features.samplerAnisotropy;

				ZEROED_STRUCT(VkPhysicalDeviceFeatures, device_features);
				device_features.shaderStorageImageExtendedFormats = extended_format_support;
				device_features.samplerAnisotropy = sampler_anisotropic;
				device_features.sampleRateShading = vulkan_globals.device_features.sampleRateShading;
				device_features.fillModeNonSolid = vulkan_globals.device_features.fillModeNonSolid;
				device_features.multiDrawIndirect = vulkan_globals.device_features.multiDrawIndirect;

				vulkan_globals.non_solid_fill = (device_features.fillModeNonSolid == VK_TRUE) ? true : false;
				vulkan_globals.multi_draw_indirect = (device_features.multiDrawIndirect == VK_TRUE) ? true : false;

				ZEROED_STRUCT(VkDeviceCreateInfo, device_create_info);
				device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				void** device_create_info_next = (void**)&device_create_info.pNext;
				if (vulkan_globals.screen_effects_sops)
					CHAIN_PNEXT(device_create_info_next, subgroup_size_control_features);
				if (vulkan_globals.ray_query)
				{
					CHAIN_PNEXT(device_create_info_next, buffer_device_address_features);
					CHAIN_PNEXT(device_create_info_next, acceleration_structure_features);
					CHAIN_PNEXT(device_create_info_next, ray_query_features);
				}
				device_create_info.queueCreateInfoCount = 1;
				device_create_info.pQueueCreateInfos = &queue_create_info;
				device_create_info.enabledExtensionCount = numEnabledExtensions;
				device_create_info.ppEnabledExtensionNames = device_extensions;
				device_create_info.pEnabledFeatures = &device_features;

				err = vkCreateDevice(engine->gl->vulkan_physical_device, &device_create_info, NULL, &vulkan_globals.device);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't create Vulkan device");

				GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
				GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
				GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
				GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
				GET_DEVICE_PROC_ADDR(QueuePresentKHR);

				SDL_Log("Device extensions:\n");
				for (i = 0; i < numEnabledExtensions; ++i)
					SDL_Log(" %s\n", device_extensions[i]);

#if defined(VK_EXT_full_screen_exclusive)
				if (vulkan_globals.full_screen_exclusive)
				{
					GET_DEVICE_PROC_ADDR(AcquireFullScreenExclusiveModeEXT);
					GET_DEVICE_PROC_ADDR(ReleaseFullScreenExclusiveModeEXT);
				}
#endif
				if (vulkan_globals.ray_query)
				{
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_get_buffer_device_address, vkGetBufferDeviceAddressKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_get_acceleration_structure_build_sizes, vkGetAccelerationStructureBuildSizesKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_create_acceleration_structure, vkCreateAccelerationStructureKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_destroy_acceleration_structure, vkDestroyAccelerationStructureKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_build_acceleration_structures, vkCmdBuildAccelerationStructuresKHR);
				}
#ifdef _DEBUG
				if (vulkan_globals.debug_utils)
				{
					GET_INSTANCE_PROC_ADDR(SetDebugUtilsObjectNameEXT);
					GET_GLOBAL_INSTANCE_PROC_ADDR(vk_cmd_begin_debug_utils_label, vkCmdBeginDebugUtilsLabelEXT);
					GET_GLOBAL_INSTANCE_PROC_ADDR(vk_cmd_end_debug_utils_label, vkCmdEndDebugUtilsLabelEXT);
				}
#endif

				vkGetDeviceQueue(vulkan_globals.device, vulkan_globals.gfx_queue_family_index, 0, &vulkan_globals.queue);

				VkFormatProperties format_properties;

				// Find color buffer format
				vulkan_globals.color_format = VK_FORMAT_R8G8B8A8_UNORM;

				if (extended_format_support == VK_TRUE)
				{
					vkGetPhysicalDeviceFormatProperties(engine->gl->vulkan_physical_device, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &format_properties);
					bool a2_b10_g10_r10_support = (format_properties.optimalTilingFeatures & REQUIRED_COLOR_BUFFER_FEATURES) == REQUIRED_COLOR_BUFFER_FEATURES;

					if (a2_b10_g10_r10_support)
					{
						SDL_Log("Using A2B10G10R10 color buffer format\n");
						vulkan_globals.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
					}
				}

				// Find depth format
				vkGetPhysicalDeviceFormatProperties(engine->gl->vulkan_physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &format_properties);
				bool x8_d24_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
				vkGetPhysicalDeviceFormatProperties(engine->gl->vulkan_physical_device, VK_FORMAT_D32_SFLOAT_S8_UINT, &format_properties);
				bool d32_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

				vulkan_globals.depth_format = VK_FORMAT_UNDEFINED;
				if (d32_support)
				{
					SDL_Log("Using D32_S8 depth buffer format\n");
					vulkan_globals.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
				}
				else if (x8_d24_support)
				{
					SDL_Log("Using D24_S8 depth buffer format\n");
					vulkan_globals.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
				}
				else
				{
					// This cannot happen with a compliant Vulkan driver. The spec requires support for one of the formats.
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Cannot find VK_FORMAT_D24_UNORM_S8_UINT or VK_FORMAT_D32_SFLOAT_S8_UINT depth buffer format");
				}

				SDL_Log("\n");

				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_pipeline, vkCmdBindPipeline);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_push_constants, vkCmdPushConstants);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_descriptor_sets, vkCmdBindDescriptorSets);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_index_buffer, vkCmdBindIndexBuffer);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_vertex_buffers, vkCmdBindVertexBuffers);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_draw, vkCmdDraw);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_draw_indexed, vkCmdDrawIndexed);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_draw_indexed_indirect, vkCmdDrawIndexedIndirect);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_pipeline_barrier, vkCmdPipelineBarrier);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_copy_buffer_to_image, vkCmdCopyBufferToImage);

			}
		};
		class StagingBuffers {
		public:
			Engine* engine;

			VkCommandPool   staging_command_pool;
			vulkan_memory_t staging_memory;
			stagingbuffer_t staging_buffers[NUM_STAGING_BUFFERS];
			int			   current_staging_buffer = 0;
			int			   num_stagings_in_flight = 0;
			SDL_mutex* staging_mutex;
			SDL_cond* staging_cond;

			void CreateStagingBuffers()
			{
				int		 i;
				VkResult err;

				ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
				buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_create_info.size = vulkan_globals.staging_buffer_size;
				buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				{
					staging_buffers[i].current_offset = 0;
					staging_buffers[i].submitted = false;

					err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &staging_buffers[i].buffer);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

					engine->gl->SetObjectName((uint64_t)staging_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Staging Buffer");
				}

				VkMemoryRequirements memory_requirements;
				vkGetBufferMemoryRequirements(vulkan_globals.device, staging_buffers[0].buffer, &memory_requirements);
				const size_t aligned_size = (memory_requirements.size + memory_requirements.alignment);

				ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
				memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
				memory_allocate_info.memoryTypeIndex =
					engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

				engine->ren->R_AllocateVulkanMemory(&engine->gl->staging_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &engine->gl->num_vulkan_misc_allocations);
				engine->gl->SetObjectName((uint64_t)engine->gl->staging_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Staging Buffers");

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				{
					err = vkBindBufferMemory(vulkan_globals.device, staging_buffers[i].buffer, engine->gl->staging_memory.handle, i * aligned_size);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");
				}

				void* data;
				err = vkMapMemory(vulkan_globals.device, engine->gl->staging_memory.handle, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
					staging_buffers[i].data = (unsigned char*)data + (i * aligned_size);
			}


			StagingBuffers(Engine e) {
				engine = &e;
				engine->gl->sbuf = this;

				int		 i;
				VkResult err;

				SDL_Log("Initializing staging\n");

				CreateStagingBuffers();

				ZEROED_STRUCT(VkCommandPoolCreateInfo, command_pool_create_info);
				command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

				err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->staging_command_pool);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateCommandPool failed");

				ZEROED_STRUCT(VkCommandBufferAllocateInfo, command_buffer_allocate_info);
				command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				command_buffer_allocate_info.commandPool = engine->gl->staging_command_pool;
				command_buffer_allocate_info.commandBufferCount = NUM_STAGING_BUFFERS;

				VkCommandBuffer command_buffers[NUM_STAGING_BUFFERS];
				err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateCommandBuffers failed");

				ZEROED_STRUCT(VkFenceCreateInfo, fence_create_info);
				fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

				ZEROED_STRUCT(VkCommandBufferBeginInfo, command_buffer_begin_info);
				command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				{
					err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &staging_buffers[i].fence);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateFence failed");

					staging_buffers[i].command_buffer = command_buffers[i];

					err = vkBeginCommandBuffer(staging_buffers[i].command_buffer, &command_buffer_begin_info);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBeginCommandBuffer failed");
				}

				engine->gl->vertex_allocate_mutex = SDL_CreateMutex();
				engine->gl->index_allocate_mutex = SDL_CreateMutex();
				engine->gl->uniform_allocate_mutex = SDL_CreateMutex();
				engine->gl->storage_allocate_mutex = SDL_CreateMutex();
				engine->gl->garbage_mutex = SDL_CreateMutex();
				engine->gl->staging_mutex = SDL_CreateMutex();
				engine->gl->staging_cond = SDL_CreateCond();

			}
		};
		class DSLayouts {
		public:
			Engine* engine;
			DSLayouts(Engine e) {
				engine = &e;

				SDL_Log("Creating descriptor set layouts\n");

				VkResult err;
				ZEROED_STRUCT(VkDescriptorSetLayoutCreateInfo, descriptor_set_layout_create_info);
				descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, single_texture_layout_binding);
					single_texture_layout_binding.binding = 0;
					single_texture_layout_binding.descriptorCount = 1;
					single_texture_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					single_texture_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &single_texture_layout_binding;

					memset(&vulkan_globals.single_texture_set_layout, 0, sizeof(vulkan_globals.single_texture_set_layout));
					vulkan_globals.single_texture_set_layout.num_combined_image_samplers = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.single_texture_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single texture");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, ubo_layout_bindings);
					ubo_layout_bindings.binding = 0;
					ubo_layout_bindings.descriptorCount = 1;
					ubo_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					ubo_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &ubo_layout_bindings;

					memset(&vulkan_globals.ubo_set_layout, 0, sizeof(vulkan_globals.ubo_set_layout));
					vulkan_globals.ubo_set_layout.num_ubos_dynamic = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ubo_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.ubo_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single dynamic UBO");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, joints_buffer_layout_bindings);
					joints_buffer_layout_bindings.binding = 0;
					joints_buffer_layout_bindings.descriptorCount = 1;
					joints_buffer_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					joints_buffer_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &joints_buffer_layout_bindings;

					memset(&vulkan_globals.joints_buffer_set_layout, 0, sizeof(vulkan_globals.joints_buffer_set_layout));
					vulkan_globals.joints_buffer_set_layout.num_storage_buffers = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.joints_buffer_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.joints_buffer_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "joints buffer");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, input_attachment_layout_bindings);
					input_attachment_layout_bindings.binding = 0;
					input_attachment_layout_bindings.descriptorCount = 1;
					input_attachment_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
					input_attachment_layout_bindings.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &input_attachment_layout_bindings;

					memset(&vulkan_globals.input_attachment_set_layout, 0, sizeof(vulkan_globals.input_attachment_set_layout));
					vulkan_globals.input_attachment_set_layout.num_input_attachments = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.input_attachment_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.input_attachment_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "input attachment");
				}

				{
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, screen_effects_layout_bindings, 5);
					screen_effects_layout_bindings[0].binding = 0;
					screen_effects_layout_bindings[0].descriptorCount = 1;
					screen_effects_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					screen_effects_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[1].binding = 1;
					screen_effects_layout_bindings[1].descriptorCount = 1;
					screen_effects_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					screen_effects_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[2].binding = 2;
					screen_effects_layout_bindings[2].descriptorCount = 1;
					screen_effects_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					screen_effects_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[3].binding = 3;
					screen_effects_layout_bindings[3].descriptorCount = 1;
					screen_effects_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
					screen_effects_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[4].binding = 4;
					screen_effects_layout_bindings[4].descriptorCount = 1;
					screen_effects_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					screen_effects_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = countof(screen_effects_layout_bindings);
					descriptor_set_layout_create_info.pBindings = screen_effects_layout_bindings;

					memset(&vulkan_globals.screen_effects_set_layout, 0, sizeof(vulkan_globals.screen_effects_set_layout));
					vulkan_globals.screen_effects_set_layout.num_combined_image_samplers = 2;
					vulkan_globals.screen_effects_set_layout.num_storage_images = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.screen_effects_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.screen_effects_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "screen effects");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, single_texture_cs_write_layout_binding);
					single_texture_cs_write_layout_binding.binding = 0;
					single_texture_cs_write_layout_binding.descriptorCount = 1;
					single_texture_cs_write_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					single_texture_cs_write_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &single_texture_cs_write_layout_binding;

					memset(&vulkan_globals.single_texture_cs_write_set_layout, 0, sizeof(vulkan_globals.single_texture_cs_write_set_layout));
					vulkan_globals.single_texture_cs_write_set_layout.num_storage_images = 1;

					err = vkCreateDescriptorSetLayout(
						vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_cs_write_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.single_texture_cs_write_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single storage image");
				}

				{
					int num_descriptors = 0;
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, lightmap_compute_layout_bindings, 9);
					lightmap_compute_layout_bindings[0].binding = num_descriptors++;
					lightmap_compute_layout_bindings[0].descriptorCount = 1;
					lightmap_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					lightmap_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[1].binding = num_descriptors++;
					lightmap_compute_layout_bindings[1].descriptorCount = 1;
					lightmap_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					lightmap_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[2].binding = num_descriptors++;
					lightmap_compute_layout_bindings[2].descriptorCount = MAXLIGHTMAPS * 3 / 4;
					lightmap_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					lightmap_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[3].binding = num_descriptors++;
					lightmap_compute_layout_bindings[3].descriptorCount = 1;
					lightmap_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					lightmap_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[4].binding = num_descriptors++;
					lightmap_compute_layout_bindings[4].descriptorCount = 1;
					lightmap_compute_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					lightmap_compute_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[5].binding = num_descriptors++;
					lightmap_compute_layout_bindings[5].descriptorCount = 1;
					lightmap_compute_layout_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					lightmap_compute_layout_bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[6].binding = num_descriptors++;
					lightmap_compute_layout_bindings[6].descriptorCount = 1;
					lightmap_compute_layout_bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					lightmap_compute_layout_bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[7].binding = num_descriptors++;
					lightmap_compute_layout_bindings[7].descriptorCount = 1;
					lightmap_compute_layout_bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					lightmap_compute_layout_bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = num_descriptors;
					descriptor_set_layout_create_info.pBindings = lightmap_compute_layout_bindings;

					memset(&vulkan_globals.lightmap_compute_set_layout, 0, sizeof(vulkan_globals.lightmap_compute_set_layout));
					vulkan_globals.lightmap_compute_set_layout.num_storage_images = 1;
					vulkan_globals.lightmap_compute_set_layout.num_sampled_images = 1 + MAXLIGHTMAPS * 3 / 4;
					vulkan_globals.lightmap_compute_set_layout.num_storage_buffers = 3;
					vulkan_globals.lightmap_compute_set_layout.num_ubos_dynamic = 2;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.lightmap_compute_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "lightmap compute");

					if (vulkan_globals.ray_query)
					{
						lightmap_compute_layout_bindings[8].binding = num_descriptors++;
						lightmap_compute_layout_bindings[8].descriptorCount = 1;
						lightmap_compute_layout_bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
						lightmap_compute_layout_bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
						descriptor_set_layout_create_info.bindingCount = num_descriptors;

						vulkan_globals.lightmap_compute_rt_set_layout.num_storage_images = 1;
						vulkan_globals.lightmap_compute_rt_set_layout.num_sampled_images = 1 + MAXLIGHTMAPS * 3 / 4;
						vulkan_globals.lightmap_compute_rt_set_layout.num_storage_buffers = 3;
						vulkan_globals.lightmap_compute_rt_set_layout.num_ubos_dynamic = 2;
						vulkan_globals.lightmap_compute_rt_set_layout.num_acceleration_structures = 1;

						err = vkCreateDescriptorSetLayout(
							vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_rt_set_layout.handle);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
						engine->gl->SetObjectName((uint64_t)vulkan_globals.lightmap_compute_rt_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "lightmap compute rt");
					}
				}

				{
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, indirect_compute_layout_bindings, 4);
					indirect_compute_layout_bindings[0].binding = 0;
					indirect_compute_layout_bindings[0].descriptorCount = 1;
					indirect_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					indirect_compute_layout_bindings[1].binding = 1;
					indirect_compute_layout_bindings[1].descriptorCount = 1;
					indirect_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					indirect_compute_layout_bindings[2].binding = 2;
					indirect_compute_layout_bindings[2].descriptorCount = 1;
					indirect_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					indirect_compute_layout_bindings[3].binding = 3;
					indirect_compute_layout_bindings[3].descriptorCount = 1;
					indirect_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = countof(indirect_compute_layout_bindings);
					descriptor_set_layout_create_info.pBindings = indirect_compute_layout_bindings;

					memset(&vulkan_globals.indirect_compute_set_layout, 0, sizeof(vulkan_globals.indirect_compute_set_layout));
					vulkan_globals.indirect_compute_set_layout.num_storage_buffers = 4;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.indirect_compute_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.indirect_compute_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "indirect compute");
				}

#if defined(_DEBUG)
				if (vulkan_globals.ray_query)
				{
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, ray_debug_layout_bindings, 2);
					ray_debug_layout_bindings[0].binding = 0;
					ray_debug_layout_bindings[0].descriptorCount = 1;
					ray_debug_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					ray_debug_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					ray_debug_layout_bindings[1].binding = 1;
					ray_debug_layout_bindings[1].descriptorCount = 1;
					ray_debug_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
					ray_debug_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = countof(ray_debug_layout_bindings);
					descriptor_set_layout_create_info.pBindings = ray_debug_layout_bindings;

					memset(&vulkan_globals.ray_debug_set_layout, 0, sizeof(vulkan_globals.ray_debug_set_layout));
					vulkan_globals.ray_debug_set_layout.num_storage_images = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ray_debug_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.screen_effects_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "ray debug");
				}
#endif

			}
		};
		class DescPool {
		public:
			Engine* engine;
			DescPool(Engine e) {
				engine = &e;
				engine->gl->desc_pool = this;

				ZEROED_STRUCT_ARRAY(VkDescriptorPoolSize, pool_sizes, 9);
				pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				pool_sizes[0].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + (MAX_SANITY_LIGHTMAPS * 2) + (MAX_GLTEXTURES + 1);
				pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				pool_sizes[1].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS;
				pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				pool_sizes[2].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE;
				pool_sizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				pool_sizes[3].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE;
				pool_sizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				pool_sizes[4].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + MAX_SANITY_LIGHTMAPS * 2;
				pool_sizes[5].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				pool_sizes[5].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + (MAX_SANITY_LIGHTMAPS * 2);
				pool_sizes[6].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				pool_sizes[6].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE;
				pool_sizes[7].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				pool_sizes[7].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + (1 + MAXLIGHTMAPS * 3 / 4) * MAX_SANITY_LIGHTMAPS;
				int num_sizes = 8;
				if (vulkan_globals.ray_query)
				{
					pool_sizes[8].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
					pool_sizes[8].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + MAX_SANITY_LIGHTMAPS;
					num_sizes = 9;
				}

				ZEROED_STRUCT(VkDescriptorPoolCreateInfo, descriptor_pool_create_info);
				descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				descriptor_pool_create_info.maxSets = MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS + 128;
				descriptor_pool_create_info.poolSizeCount = num_sizes;
				descriptor_pool_create_info.pPoolSizes = pool_sizes;
				descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

				vkCreateDescriptorPool(vulkan_globals.device, &descriptor_pool_create_info, NULL, &vulkan_globals.descriptor_pool);
			}

		};
		class GPUBuffers {
		public:
			Engine* engine;
			GPUBuffers(Engine e) {
				engine = &e;
				engine->gl->gpu_buffers = this;
				SDL_Log("Creating GPU buffers\n");
				engine->ren->R_InitDynamicVertexBuffers();
				engine->ren->R_InitDynamicIndexBuffers();
				engine->ren->R_InitDynamicUniformBuffers();
				engine->ren->R_InitFanIndexBuffer();

			}
		};
		class MeshHeap {
		public:
			glheap_t* heap;
			Engine* engine;
			MeshHeap(Engine e) {
				engine = &e;
				engine->gl->mesh_heap = this;
				SDL_Log("Creating mesh heap\n");

				// Allocate index buffer & upload to GPU
				ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
				buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_create_info.size = 16;
				buffer_create_info.usage =
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				VkBuffer dummy_buffer;
				VkResult err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &dummy_buffer);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

				VkMemoryRequirements memory_requirements;
				vkGetBufferMemoryRequirements(vulkan_globals.device, dummy_buffer, &memory_requirements);

				const uint32_t memory_type_index = engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
				VkDeviceSize   heap_size = MESH_HEAP_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
				heap = engine->gl->HeapCreate(heap_size, MESH_HEAP_PAGE_SIZE, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, MESH_HEAP_NAME);

				vkDestroyBuffer(vulkan_globals.device, dummy_buffer, NULL);

			}
		};
		class TexHeap {
		public:
			Engine* engine;
			glheap_t* heap;
			TexHeap(Engine e) {
				engine = &e;
				engine->gl->tex_heap = this;
				SDL_Log("Creating texture heap\n");
				ZEROED_STRUCT(VkImageCreateInfo, image_create_info);
				image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				image_create_info.imageType = VK_IMAGE_TYPE_2D;
				image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
				image_create_info.extent.width = 1;
				image_create_info.extent.height = 1;
				image_create_info.extent.depth = 1;
				image_create_info.mipLevels = 1;
				image_create_info.arrayLayers = 1;
				image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
				image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
				image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
				image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

				VkImage	 dummy_image;
				VkResult err = vkCreateImage(vulkan_globals.device, &image_create_info, NULL, &dummy_image);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateImage failed");

				VkMemoryRequirements memory_requirements;
				vkGetImageMemoryRequirements(vulkan_globals.device, dummy_image, &memory_requirements);
				const uint32_t memory_type_index = engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

				const VkDeviceSize heap_memory_size = TEXTURE_HEAP_MEMORY_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
				heap = engine->gl->HeapCreate(heap_memory_size, TEXTURE_HEAP_PAGE_SIZE, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, "Texture Heap");

				vkDestroyImage(vulkan_globals.device, dummy_image, NULL);

			}
		};

		Instance* instance = nullptr;
		Device* device = nullptr;
		CommandBuffers* cbuf = nullptr;
		StagingBuffers* sbuf = nullptr;
		DSLayouts* dslayouts = nullptr;
		DescPool* desc_pool = nullptr;
		GPUBuffers* gpu_buffers = nullptr;
		MeshHeap* mesh_heap = nullptr;
		TexHeap* tex_heap = nullptr;

		GL(Engine e) {
			engine = &e;
			engine->gl = this;

			if (engine->ren == nullptr) {
				engine->ren = new REN(*engine);
			}

			instance = new Instance(*engine);
			device = new Device(*engine);
			cbuf = new CommandBuffers(*engine);
			vulkan_globals.staging_buffer_size = INITIAL_STAGING_BUFFER_SIZE_KB * 1024;
			sbuf = new StagingBuffers(*engine);
			dslayouts = new DSLayouts(*engine);
			desc_pool = new DescPool(*engine);
			gpu_buffers = new GPUBuffers(*engine);
			mesh_heap = new MeshHeap(*engine);
			tex_heap = new TexHeap(*engine);
			engine->ren->R_InitSamplers();
			engine->ren->R_CreatePipelineLayouts();
			R_CreatePaletteOctreeBuffers(palette_octree_colors, NUM_PALETTE_OCTREE_COLORS, palette_octree_nodes, NUM_PALETTE_OCTREE_NODES);

		}

	};
	class VID {
	public:
		Engine* engine;
		SDL_Window* draw_context;
		VID(Engine e) {
			engine = &e;
			engine->vid = this;

			// Initialize SDL
			if (SDL_Init(SDL_INIT_VIDEO) < 0) {
				SDL_Log("what the bitch!!!! SDL_Error: %s\n", SDL_GetError());
			}

			// Load the Vulkan library
			SDL_Vulkan_LoadLibrary(nullptr);

			// Create a window
			draw_context = SDL_CreateWindow("Tremor Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_VULKAN);

			engine->gl = new GL(*engine);

			
		}
	};
	class COM {
	public:
		Engine* engine;
		uint32_t xorshiro_state[2] = { 0xcdb38550, 0x720a8392 };

		
		COM(Engine e) {
			engine = &e;
			engine->com = this;
		}

		int CheckParmNext(int last, const char* parm)
		{
			int i;

			for (i = last + 1; i < engine->argc; i++)
			{
				if (!engine->argv[i])
					continue; // NEXTSTEP sometimes clears appkit vars.
				if (!strcmp(parm, engine->argv[i]))
					return i;
			}

			return 0;
		}
		int CheckParm(const char* parm)
		{
			return CheckParmNext(0, parm);
		}

		void SeedRand(uint64_t seed) // probably broken :(
		{
			// Xorshiro128+
			uint64_t s0 = seed + 0x9e3779b97f4a7c15;
			uint64_t s1 = seed + 0x9e3779b97f4a7c15;
			s0 ^= (s0 << 23) ^ (s1 >> 17) ^ (s1 << 26);
			s1 ^= (s1 << 23) ^ (s0 >> 17) ^ (s0 << 26);
			xorshiro_state[0] = (uint32_t)s0;
			xorshiro_state[1] = (uint32_t)(s1 >> 32);
		}

		uint32_t Rand() //xorshiro was broken, so it can go to hell :)
		{
			std::random_device rd;  // a seed source for the random number engine
			std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
			std::uniform_int_distribution<> distrib(0, 0xFFFFFF);

			uint32_t bla = distrib(gen);

			SDL_Log("rand: %d\n", bla);
			return bla;
		}
	};
	class Host {
	public:
		Engine* engine;

		jmp_buf abortserver;


		Host(Engine e) {
			engine = &e;
		}

		void Frame(double time) {

			double before = DoubleTime();

			double accumtime = 0;
			if (setjmp(abortserver)) {
				return;
			}

			engine->com->Rand();

			double after = DoubleTime();

			double delta = after - before;
			engine->ticks++;
		}
	};

	VID* vid = nullptr;
	GL* gl = nullptr;
	COM* com = nullptr;
	Tasks* tasks = nullptr;
	Host* host = nullptr;
	REN* ren = nullptr;

	int argc; char** argv;

	Engine(int ct, char* var[]) {	

		max_thread_stack_alloc_size = MAX_STACK_ALLOC_SIZE;

		argc = ct; argv = var;

		tasks = new Tasks(*this);
		com = new COM(*this);
		host = new Host(*this);

		vid = new VID(*this);
		gl = new GL(*this);
		// we already init this in GL
		if (ren == nullptr) {
			ren = new REN(*this);
		}

	}

};

int main(int argc, char* argv[]) {


	auto t = new Engine(argc, argv);



	double oldtime{0}, newtime{0};

	while (1) {
		

		SDL_Event event{};
		while (SDL_PollEvent(&event)) {

			if (event.type == SDL_QUIT) {
				SDL_DestroyWindow(t->vid->draw_context);
				SDL_Quit();
				return 0;
			}
		}			
		SDL_Delay(4); // Simulate a frame delay
		newtime = DoubleTime();
		double curtime = newtime - oldtime;
		t->host->Frame(curtime);
		oldtime = newtime;

	}

	return 0;
}