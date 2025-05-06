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


#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace vkQuake {
	namespace Net {

		// NetAddress implementation
		NetAddress::NetAddress(std::string_view address, uint16_t port) : port_(port) {
			// Parse the address string
			if (address.empty() || address == "localhost") {
				*this = Loopback(port);
				return;
			}

			// Check if this is an IPv4 address in a.b.c.d format
			struct sockaddr_in sa;
			if (inet_pton(AF_INET, address.data(), &(sa.sin_addr)) == 1) {
				// It's a valid IPv4 address
				is_ipv6_ = false;
				std::memcpy(address_.data(), &sa.sin_addr, 4);
			}
			else {
				*this = FromHostname(address, port);
			}
		}

		NetAddress NetAddress::FromIPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
			NetAddress addr;
			addr.address_[0] = a;
			addr.address_[1] = b;
			addr.address_[2] = c;
			addr.address_[3] = d;
			addr.port_ = port;
			addr.is_ipv6_ = false;
			return addr;
		}

		NetAddress NetAddress::FromHostname(std::string_view hostname, uint16_t port) {
			NetAddress addr;
			addr.port_ = port;

			struct addrinfo hints {}, * res = nullptr;
			std::memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;  // Allow IPv4 or IPv6
			hints.ai_socktype = SOCK_DGRAM;

			// Convert string_view to C-style string for getaddrinfo
			std::string temp(hostname);
			int status = getaddrinfo(temp.c_str(), nullptr, &hints, &res);

			if (status != 0) {
				// Failed to resolve hostname, return loopback as fallback
				return Loopback(port);
			}

			if (res->ai_family == AF_INET) {
				// IPv4 address
				struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
				std::memcpy(addr.address_.data(), &ipv4->sin_addr, 4);
				addr.is_ipv6_ = false;
			}
			else if (res->ai_family == AF_INET6) {
				// IPv6 address
				struct sockaddr_in6* ipv6 = reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
				std::memcpy(addr.address_.data(), &ipv6->sin6_addr, 16);
				addr.is_ipv6_ = true;
			}

			freeaddrinfo(res);
			return addr;
		}

		NetAddress NetAddress::Loopback(uint16_t port) {
			NetAddress addr;
			addr.address_[0] = 127;
			addr.address_[1] = 0;
			addr.address_[2] = 0;
			addr.address_[3] = 1;
			addr.port_ = port;
			addr.is_ipv6_ = false;
			return addr;
		}

		bool NetAddress::operator==(const NetAddress& other) const noexcept {
			if (is_ipv6_ != other.is_ipv6_) {
				return false;
			}

			size_t size = is_ipv6_ ? 16 : 4;
			return (port_ == other.port_) &&
				(std::memcmp(address_.data(), other.address_.data(), size) == 0);
		}

		bool NetAddress::operator!=(const NetAddress& other) const noexcept {
			return !(*this == other);
		}

		std::string NetAddress::ToString() const {
			if (is_ipv6_) {
				// IPv6 formatting
				char addrStr[INET6_ADDRSTRLEN];
				inet_ntop(AF_INET6, address_.data(), addrStr, INET6_ADDRSTRLEN);
				return std::string(addrStr) + ":" + std::to_string(port_);
			}
			else {
				// IPv4 formatting
				return std::to_string(address_[0]) + "." +
					std::to_string(address_[1]) + "." +
					std::to_string(address_[2]) + "." +
					std::to_string(address_[3]) + ":" +
					std::to_string(port_);
			}
		}

		// MessageBuffer implementation
		MessageBuffer::MessageBuffer() : MessageBuffer(MAX_MSGLEN) {}

		MessageBuffer::MessageBuffer(size_t initialSize)
			: buffer_(initialSize), readPos_(0), writePos_(0) {
		}

		int8_t MessageBuffer::ReadByte() {
			if (!CanRead(sizeof(int8_t))) {
				throw std::runtime_error("Buffer underflow when reading byte");
			}

			int8_t value = static_cast<int8_t>(buffer_[readPos_]);
			readPos_ += sizeof(int8_t);
			return value;
		}

		int16_t MessageBuffer::ReadShort() {
			if (!CanRead(sizeof(int16_t))) {
				throw std::runtime_error("Buffer underflow when reading short");
			}

			int16_t value;
			std::memcpy(&value, buffer_.data() + readPos_, sizeof(int16_t));
			readPos_ += sizeof(int16_t);
			return ntohs(value);  // Convert from network byte order
		}

		int32_t MessageBuffer::ReadLong() {
			if (!CanRead(sizeof(int32_t))) {
				throw std::runtime_error("Buffer underflow when reading long");
			}

			int32_t value;
			std::memcpy(&value, buffer_.data() + readPos_, sizeof(int32_t));
			readPos_ += sizeof(int32_t);
			return ntohl(value);  // Convert from network byte order
		}

		float MessageBuffer::ReadFloat() {
			if (!CanRead(sizeof(float))) {
				throw std::runtime_error("Buffer underflow when reading float");
			}

			// Quake stores floats as a 32-bit integer, so we need to convert
			int32_t intValue = ReadLong();
			float value;
			std::memcpy(&value, &intValue, sizeof(float));
			return value;
		}

		std::string MessageBuffer::ReadString() {
			std::string result;
			char c;

			while (readPos_ < writePos_ && (c = static_cast<char>(buffer_[readPos_++])) != '\0') {
				result.push_back(c);
			}

			return result;
		}

		std::string MessageBuffer::ReadStringLine() {
			std::string result;
			char c;

			while (readPos_ < writePos_ &&
				(c = static_cast<char>(buffer_[readPos_++])) != '\n' &&
				c != '\0') {
				result.push_back(c);
			}

			return result;
		}

		void MessageBuffer::WriteByte(int8_t value) {
			EnsureCapacity(writePos_ + sizeof(int8_t));
			buffer_[writePos_] = static_cast<uint8_t>(value);
			writePos_ += sizeof(int8_t);
		}

		void MessageBuffer::WriteShort(int16_t value) {
			EnsureCapacity(writePos_ + sizeof(int16_t));
			int16_t netValue = htons(value);  // Convert to network byte order
			std::memcpy(buffer_.data() + writePos_, &netValue, sizeof(int16_t));
			writePos_ += sizeof(int16_t);
		}

		void MessageBuffer::WriteLong(int32_t value) {
			EnsureCapacity(writePos_ + sizeof(int32_t));
			int32_t netValue = htonl(value);  // Convert to network byte order
			std::memcpy(buffer_.data() + writePos_, &netValue, sizeof(int32_t));
			writePos_ += sizeof(int32_t);
		}

		void MessageBuffer::WriteFloat(float value) {
			// Quake stores floats as a 32-bit integer
			int32_t intValue;
			std::memcpy(&intValue, &value, sizeof(float));
			WriteLong(intValue);
		}

		void MessageBuffer::WriteString(std::string_view str) {
			EnsureCapacity(writePos_ + str.size() + 1);  // +1 for null terminator

			// Copy string data
			std::memcpy(buffer_.data() + writePos_, str.data(), str.size());
			writePos_ += str.size();

			// Add null terminator
			buffer_[writePos_] = 0;
			writePos_ += 1;
		}

		void MessageBuffer::WriteData(std::span<const uint8_t> data) {
			EnsureCapacity(writePos_ + data.size());
			std::memcpy(buffer_.data() + writePos_, data.data(), data.size());
			writePos_ += data.size();
		}

		void MessageBuffer::Clear() {
			readPos_ = 0;
			writePos_ = 0;
		}

		void MessageBuffer::EnsureCapacity(size_t requiredSize) {
			if (requiredSize > buffer_.size()) {
				// Double the buffer size until it's big enough
				size_t newSize = buffer_.size();
				while (newSize < requiredSize) {
					newSize *= 2;
				}

				// Cap at maximum message size
				newSize = std::min(newSize, static_cast<size_t>(MAX_MSGLEN));

				if (requiredSize > newSize) {
					throw std::runtime_error("Message would exceed maximum size");
				}

				buffer_.resize(newSize);
			}
		}

		// NetworkMessage implementation
		NetworkMessage::NetworkMessage(MessageHeader header) : header_(header) {}

		// QuakeProtocolHandler implementation
		QuakeProtocolHandler::QuakeProtocolHandler() : protocolVersion_(PROTOCOL_VERSION), outgoingSequence_(0) {}

		std::optional<NetworkMessage> QuakeProtocolHandler::ProcessIncoming(
			std::span<const uint8_t> data, const NetAddress& sender) {

			// Validate minimum packet size
			if (data.size() < 8) {  // Minimum header size
				return std::nullopt;
			}

			// Extract header from the packet
			MessageHeader header = ExtractHeader(data);

			// Create a message with the header
			NetworkMessage message(header);

			// Copy message data (skip the header)
			size_t headerSize = 8;  // Size of basic header
			if (header.reliability == ReliabilityType::Reliable) {
				headerSize += 4;  // Additional sequence fields
			}

			if (data.size() > headerSize) {
				message.GetBuffer().WriteData(data.subspan(headerSize));
			}

			return message;
		}

		std::vector<uint8_t> QuakeProtocolHandler::PrepareOutgoing(
			const NetworkMessage& msg, const NetAddress& recipient) {

			// Initialize buffer with enough capacity
			std::vector<uint8_t> buffer;
			buffer.reserve(msg.GetSize() + 16);  // Message size + header size

			// Get a copy of the header to modify
			MessageHeader header = msg.GetHeader();

			// Set sequence number for reliable messages
			if (header.reliability == ReliabilityType::Reliable) {
				header.sequence = ++outgoingSequence_;
			}

			// Encode the header into the buffer
			EncodeHeader(buffer, header);

			// Append message data
			auto messageData = msg.GetBuffer().GetData();
			buffer.insert(buffer.end(), messageData.begin(), messageData.end());

			return buffer;
		}

		MessageHeader QuakeProtocolHandler::ExtractHeader(std::span<const uint8_t> data) {
			MessageHeader header;

			// First byte is packet type
			header.type = static_cast<PacketType>(data[0]);

			// Second byte is reliability
			header.reliability = static_cast<ReliabilityType>(data[1]);

			// Next two bytes are size
			uint16_t size;
			std::memcpy(&size, data.data() + 2, sizeof(uint16_t));
			header.size = ntohs(size);

			// For reliable packets, extract sequence and ack
			if (header.reliability == ReliabilityType::Reliable && data.size() >= 12) {
				uint32_t sequence, ack;
				std::memcpy(&sequence, data.data() + 4, sizeof(uint32_t));
				std::memcpy(&ack, data.data() + 8, sizeof(uint32_t));
				header.sequence = ntohl(sequence);
				header.ack = ntohl(ack);
			}

			return header;
		}

		void QuakeProtocolHandler::EncodeHeader(std::vector<uint8_t>& buffer, const MessageHeader& header) {
			// Reserve space for the header
			size_t headerSize = (header.reliability == ReliabilityType::Reliable) ? 12 : 4;
			buffer.resize(headerSize);

			// Packet type
			buffer[0] = static_cast<uint8_t>(header.type);

			// Reliability
			buffer[1] = static_cast<uint8_t>(header.reliability);

			// Size (in network byte order)
			uint16_t netSize = htons(header.size);
			std::memcpy(buffer.data() + 2, &netSize, sizeof(uint16_t));

			// For reliable packets, add sequence and ack
			if (header.reliability == ReliabilityType::Reliable) {
				// Sequence (in network byte order)
				uint32_t netSequence = htonl(header.sequence);
				std::memcpy(buffer.data() + 4, &netSequence, sizeof(uint32_t));

				// Ack (in network byte order)
				uint32_t netAck = htonl(header.ack);
				std::memcpy(buffer.data() + 8, &netAck, sizeof(uint32_t));
			}
		}

	} // namespace Net
} // namespace vkQuake

class Engine {
public:

	bool isDedicated;

	server_t		sv;
	server_static_t svs;

	PollProcedure* pollProcedureList = NULL;

	void SchedulePollProcedure(PollProcedure* proc, double timeOffset)
	{
		PollProcedure* pp, * prev;

		proc->nextTime = DoubleTime() + timeOffset;
		for (pp = pollProcedureList, prev = NULL; pp; pp = pp->next)
		{
			if (pp->nextTime >= proc->nextTime)
				break;
			prev = pp;
		}

		if (prev == NULL)
		{
			proc->next = pollProcedureList;
			pollProcedureList = proc;
			return;
		}

		proc->next = pp;
		prev->next = proc;
	}

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

		Tasks(Engine* e){
			engine = e;
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
				const int num_task_workers = (task->task_type == TASK_TYPE_INDEXED) ? std::min(task->indexed_limit, num_workers) : 1;
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
				bool indexed_task = task->task_type == TASK_TYPE_INDEXED;
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
			void* tdata = bla->data;

			delete bla;

			engine->tasks->Task_Worker(engine, tdata);
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
			num_workers = CLAMP(f, SDL_GetCPUCount(), TASKS_MAX_WORKERS);

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
				chucklenuts* bla = new chucklenuts();
				bla->data = (void*)(intptr_t)i;
				bla->engine = engine;
				worker_threads[i] = SDL_CreateThread(Wrapper, "Task_Worker", bla);
			}
			SDL_Log("Created %d worker threads.\n", num_workers);
		}


	};
	class REN {
	public:
		Engine* engine;
		REN(Engine* e) {
			engine = e;
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


			Instance(Engine* e) {
				engine = e;
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
			CommandBuffers(Engine* e) {
				engine = e;
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
			Device(Engine* e) {
				engine = e;
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


			StagingBuffers(Engine* e) {
				engine = e;
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
			DSLayouts(Engine* e) {
				engine = e;

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
			DescPool(Engine* e) {
				engine = e;
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
			GPUBuffers(Engine* e) {
				engine = e;
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
			MeshHeap(Engine* e) {
				engine = e;
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
			TexHeap(Engine* e) {
				engine = e;
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

		GL(Engine* e) {
			engine = e;
			engine->gl = this;

			if (engine->ren == nullptr) {
				engine->ren = new REN(engine);
			}

			instance = new Instance(engine);
			device = new Device(engine);
			cbuf = new CommandBuffers(engine);
			vulkan_globals.staging_buffer_size = INITIAL_STAGING_BUFFER_SIZE_KB * 1024;
			sbuf = new StagingBuffers(engine);
			dslayouts = new DSLayouts(engine);
			desc_pool = new DescPool(engine);
			gpu_buffers = new GPUBuffers(engine);
			mesh_heap = new MeshHeap(engine);
			tex_heap = new TexHeap(engine);
			engine->ren->R_InitSamplers();
			engine->ren->R_CreatePipelineLayouts();
			R_CreatePaletteOctreeBuffers(palette_octree_colors, NUM_PALETTE_OCTREE_COLORS, palette_octree_nodes, NUM_PALETTE_OCTREE_NODES);

		}

	};
	class VID {
	public:
		Engine* engine;
		SDL_Window* draw_context;
		VID(Engine* e) {
			engine = e;
			engine->vid = this;

			// Initialize SDL
			if (SDL_Init(SDL_INIT_VIDEO) < 0) {
				SDL_Log("what the bitch!!!! SDL_Error: %s\n", SDL_GetError());
			}

			// Load the Vulkan library
			SDL_Vulkan_LoadLibrary(nullptr);

			// Create a window
			draw_context = SDL_CreateWindow("Tremor Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_VULKAN);

			engine->gl = new GL(engine);

			
		}
	};
	class COM {
	public:
		Engine* engine;
		uint32_t xorshiro_state[2] = { 0xcdb38550, 0x720a8392 };

		bool multiuser;
		
		searchpath_t* searchpaths;
		searchpath_t* base_searchpaths;

		cvar_t protocolname = { "engine->com->protocolname", "Tremor" };

		const char* ParseEx(const char* data, cpe_mode mode)
		{
			int c;
			int len;

			len = 0;
			com_token[0] = 0;

			if (!data)
				return NULL;

			// skip whitespace
		skipwhite:
			while ((c = *data) <= ' ')
			{
				if (c == 0)
					return NULL; // end of file
				data++;
			}

			// skip // comments
			if (c == '/' && data[1] == '/')
			{
				while (*data && *data != '\n')
					data++;
				goto skipwhite;
			}

			// skip /*..*/ comments
			if (c == '/' && data[1] == '*')
			{
				data += 2;
				while (*data && !(*data == '*' && data[1] == '/'))
					data++;
				if (*data)
					data += 2;
				goto skipwhite;
			}

			// handle quoted strings specially
			if (c == '\"')
			{
				data++;
				while (1)
				{
					if ((c = *data) != 0)
						++data;
					if (c == '\"' || !c)
					{
						com_token[len] = 0;
						return data;
					}
					if (len < countof(com_token) - 1)
						com_token[len++] = c;
					else if (mode == CPE_NOTRUNC)
						return NULL;
				}
			}

			// parse single characters
			if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
			{
				if (len < countof(com_token) - 1)
					com_token[len++] = c;
				else if (mode == CPE_NOTRUNC)
					return NULL;
				com_token[len] = 0;
				return data + 1;
			}

			// parse a regular word
			do
			{
				if (len < countof(com_token) - 1)
					com_token[len++] = c;
				else if (mode == CPE_NOTRUNC)
					return NULL;
				data++;
				c = *data;
				/* commented out the check for ':' so that ip:port works */
				if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' /* || c == ':' */)
					break;
			} while (c > 32);

			com_token[len] = 0;
			return data;
		}


		const char* Parse(const char* data)
		{
			return ParseEx(data, CPE_NOTRUNC);
		}

		COM(Engine* e) {
			engine = e;
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

		uint32_t Rand() //yeah yeah, i know xorshiro was faster
			// and more memory efficient on top of that as well
			// i'll add choices for RNG methods eventually
		{
			std::random_device rd;  // a seed source for the random number engine
			std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
			std::uniform_int_distribution<> distrib(0, 0xFFFFFF);

			uint32_t bla = distrib(gen);

			SDL_Log("rand: %d\n", bla);
			return bla;
		}

		const char* GetGameNames(bool full)
		{
			if (full)
			{
				if (*com_gamenames)
					return va("%s;%s", GAMENAME, com_gamenames);
				else
					return GAMENAME;
			}
			return com_gamenames;
			//	return COM_SkipPath(com_gamedir);
		}

		const char* FileGetExtension(const char* in)
		{
			const char* src;
			size_t		len;

			len = strlen(in);
			if (len < 2) /* nothing meaningful */
				return "";

			src = in + len - 1;
			while (src != in && src[-1] != '.')
				src--;
			if (src == in || strchr(src, '/') != NULL || strchr(src, '\\') != NULL)
				return ""; /* no extension, or parent directory has a dot */

			return src;
		}
		void StripExtension(const char* in, char* out, size_t outsize)
		{
			int length;

			if (!*in)
			{
				*out = '\0';
				return;
			}
			if (in != out) /* copy when not in-place editing */
				q_strlcpy(out, in, outsize);
			length = (int)strlen(out) - 1;
			while (length > 0 && out[length] != '.')
			{
				--length;
				if (out[length] == '/' || out[length] == '\\')
					return; /* no extension */
			}
			if (length > 0)
				out[length] = '\0';
		}
	};
	class CL {
	public:
		client_static_t* s;
		client_state_t* state;
		Engine* engine;

		filelist_item_t* demolist;

		CL(Engine* e) {
			engine = e;
			engine->cl = this;

			s = (new client_static_t());
			state = (new client_state_t());
		}

		void FinishTimeDemo(void)
		{
			int	  frames;
			float time;

			s->timedemo = false;

			// the first frame didn't count
			frames = (engine->ticks - s->td_startframe) - 1;
			time = engine->host->realtime - s->td_starttime;
			if (!time)
				time = 1;
			SDL_Log("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames / time);
		}

		void WriteDemoMessage(void)
		{
			int	  len;
			int	  i;
			float f;

			len = LittleLong(engine->net->message.cursize);
			fwrite(&len, 4, 1, s->demofile);
			for (i = 0; i < 3; i++)
			{
				f = LittleFloat(state->viewangles[i]);
				fwrite(&f, 4, 1, s->demofile);
			}
			fwrite(engine->net->message.data, engine->net->message.cursize, 1, s->demofile);
			fflush(s->demofile);
		}

		void FileList_Add(const char* name, filelist_item_t** list)
		{
			filelist_item_t* item, * cursor, * prev;

			// ignore duplicate
			for (item = *list; item; item = item->next)
			{
				if (!strcmp(name, item->name))
					return;
			}

			item = (filelist_item_t*)Mem_Alloc(sizeof(filelist_item_t));
			q_strlcpy(item->name, name, sizeof(item->name));

			// insert each entry in alphabetical order
			if (*list == NULL || q_strcasecmp(item->name, (*list)->name) < 0) // insert at front
			{
				item->next = *list;
				*list = item;
			}
			else // insert later
			{
				prev = *list;
				cursor = (*list)->next;
				while (cursor && (q_strcasecmp(item->name, cursor->name) > 0))
				{
					prev = cursor;
					cursor = cursor->next;
				}
				item->next = prev->next;
				prev->next = item;
			}
		}


		void FileList_Init(char* path, char* ext, int minsize, filelist_item_t** list)
		{
#ifdef _WIN32
			WIN32_FIND_DATA fdat;
			HANDLE			fhnd;
#else
			DIR* dir_p;
			struct dirent* dir_t;
#endif
			char		  filestring[MAX_OSPATH];
			char		  filename[32];
			char		  ignorepakdir[32];
			searchpath_t* search;
			pack_t* pak;
			int			  i;
			searchpath_t  multiuser_saves;

			if (engine->com->multiuser && !strcmp(ext, "sav"))
			{
				char* pref_path = SDL_GetPrefPath("Tremor", engine->com->GetGameNames(true));
				strcpy_s(multiuser_saves.filename, pref_path);
				SDL_free(pref_path);
				multiuser_saves.next = engine->com->searchpaths;
			}
			else
				multiuser_saves.next = NULL;

			// we don't want to list the files in id1 pakfiles,
			// because these are not "add-on" files
			_snprintf_s(ignorepakdir, sizeof(ignorepakdir), "/%s/", GAMENAME);

			for (search = (multiuser_saves.next ? &multiuser_saves : engine->com->searchpaths); search; search = search->next)
			{
				if (*search->filename) // directory
				{
#ifdef _WIN32
					_snprintf_s(filestring, sizeof(filestring), "%s/%s*.%s", search->filename, path, ext);
					fhnd = FindFirstFile((LPCWSTR)filestring, &fdat);
					if (fhnd == INVALID_HANDLE_VALUE)
						goto next;
					do
					{
						engine->com->StripExtension((const char*)fdat.cFileName, filename, sizeof(filename));
						FileList_Add(filename, list);
					} while (FindNextFile(fhnd, &fdat));
					FindClose(fhnd);
#else
					q_snprintf(filestring, sizeof(filestring), "%s/%s", search->filename, path);
					dir_p = opendir(filestring);
					if (dir_p == NULL)
						goto next;
					while ((dir_t = readdir(dir_p)) != NULL)
					{
						if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), ext) != 0)
							continue;
						COM_StripExtension(dir_t->d_name, filename, sizeof(filename));
						FileList_Add(filename, list);
					}
					closedir(dir_p);
#endif
				next:
					if (!strcmp(ext, "sav") && (!engine->com->multiuser || search != &multiuser_saves)) // only game dir for savegames
						break;
				}
				else // pakfile
				{
					if (!strstr(search->pack->filename, ignorepakdir))
					{ // don't list standard id maps
						for (i = 0, pak = search->pack; i < pak->numfiles; i++)
						{
							if (!strcmp(engine->com->FileGetExtension(pak->files[i].name), ext))
							{
								if (pak->files[i].filelen > minsize)
								{ // don't list files under minsize (ammo boxes etc for maps)
									engine->com->StripExtension(pak->files[i].name + strlen(path), filename, sizeof(filename));
									FileList_Add(filename, list);
								}
							}
						}
					}
				}
			}
		}


		void StopPlayback(void)
		{
			if (!s->demoplayback)
				return;

			fclose(s->demofile);
			s->demoplayback = false;
			s->demoseeking = false;
			s->demopaused = false;
			s->demofile = NULL;
			s->state = ca_disconnected;
			s->demo_prespawn_end = 0;

			if (s->timedemo)
				FinishTimeDemo();
		}



		void DemoList_Clear(void)
		{
			FileList_Clear(&demolist);
		}

		void DemoList_Init(void)
		{
			FileList_Init((char*)"", (char*)"dem", 0, &demolist);
		}

		void DemoList_Rebuild(void)
		{
			DemoList_Clear();
			DemoList_Init();
		}


		void Stop_f(void)
		{
			if (engine->cmd->source != src_command)
				return;

			if (!s->demorecording)
			{
				SDL_Log("Not recording a demo.\n");
				return;
			}

			// write a disconnect message to the demo file
			engine->sz->Clear(&engine->net->message);
			engine->msg->WriteByte(&engine->net->message, svc_disconnect);
			WriteDemoMessage();

			// finish up
			fclose(s->demofile);
			s->demofile = NULL;
			s->demorecording = false;
			SDL_Log("Completed demo\n");

			// ericw -- update demo tab-completion list
			DemoList_Rebuild();
		}

		void Disconnect(void)
		{
			//if (key_dest == key_message)
			//	Key_EndChat(); // don't get stuck in chat mode

			// stop sounds (especially looping!)
			//S_StopAllSounds(true, false);
			//BGM_Stop();
			//CDAudio_Stop();

			// if running a local server, shut it down
			if (s->demoplayback)
				StopPlayback();
			else if (s->state == ca_connected)
			{
				if (s->demorecording)
					Stop_f();

				SDL_Log("Sending clc_disconnect\n");
				engine->sz->Clear(&s->message);
				engine->msg->WriteByte(&s->message, clc_disconnect);
				engine->net->SendUnreliableMessage(s->netcon, &s->message);
				engine->sz->Clear(&s->message);
				engine->net->Close(s->netcon);
				s->netcon = NULL;

				s->state = ca_disconnected;
				if (engine->sv.active)
					engine->host->ShutdownServer(false);
			}

			s->demoplayback = s->timedemo = false;
			s->demopaused = false;
			s->signon = 0;
			s->netcon = NULL;
			state->intermission = 0;
			state->worldmodel = NULL;
			state->sendprespawn = false;
			//SCR_CenterPrintClear();
		}



	};
	class Sys {
	public:
		Engine* engine;
		Sys(Engine* e) {
			engine = e;
			engine->sys = this;


		}
		void SetTimerResolution(void)
		{
			/* Set OS timer resolution to 1ms.
			   Works around buffer underruns with directsound and SDL2, but also
			   will make Sleep()/SDL_Dleay() accurate to 1ms which should help framerate
			   stability.
			*/
			timeBeginPeriod(1);
		}

		const char* ConsoleInput(void)
		{
			static char	 con_text[256];
			static int	 textlen;
			INPUT_RECORD recs[1024];
			int			 ch;
			DWORD		 dummy, numread, numevents;

			for (;;)
			{
				if (GetNumberOfConsoleInputEvents(hinput, &numevents) == 0)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Error getting # of console events");

				if (!numevents)
					break;

				if (ReadConsoleInput(hinput, recs, 1, &numread) == 0)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Error reading console input");

				if (numread != 1)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Couldn't read console input");

				if (recs[0].EventType == KEY_EVENT)
				{
					if (recs[0].Event.KeyEvent.bKeyDown == TRUE)
					{
						ch = recs[0].Event.KeyEvent.uChar.AsciiChar;
						if (ch && recs[0].Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)
						{
							BYTE keyboard[256] = { 0 };
							WORD output;
							keyboard[SHIFT_PRESSED] = 0x80;
							if (ToAscii(VkKeyScan(ch), 0, keyboard, &output, 0) == 1)
								ch = output;
						}

						switch (ch)
						{
						case '\r':
							WriteFile(houtput, "\r\n", 2, &dummy, NULL);

							if (textlen != 0)
							{
								con_text[textlen] = 0;
								textlen = 0;
								return con_text;
							}

							break;

						case '\b':
							WriteFile(houtput, "\b \b", 3, &dummy, NULL);
							if (textlen != 0)
								textlen--;

							break;

						default:
							if (ch >= ' ')
							{
								WriteFile(houtput, &ch, 1, &dummy, NULL);
								con_text[textlen] = ch;
								textlen = (textlen + 1) & 0xff;
							}

							break;
						}
					}
				}
			}

			return NULL;
		}

	};
	class Cmd {
	public:
		sizebuf_t text;
		Engine* engine;
		cmd_source_t source;

		int		   argc;
		char		   argv[MAX_ARGS][1024];
		const char*		   null_string = NULL;
		const char* args = NULL;

		int Argc(void)
		{
			return argc;
		}


		const char* Argv(int arg)
		{
			if (arg < 0 || arg >= argc)
				return null_string;
			return argv[arg];
		}

		const char* Args(void)
		{
			if (!args)
				return "";
			return args;
		}

		void TokenizeString(const char* text)
		{
			int i;

			// clear the args from the last string
			for (i = 0; i < argc; i++)
				argv[i][0] = 0;

			argc = 0;
			args = NULL;

			while (1)
			{
				// skip whitespace up to a /n
				while (*text && *text <= ' ' && *text != '\n')
				{
					text++;
				}

				if (*text == '\n')
				{ // a newline seperates commands in the buffer
					text++;
					break;
				}

				if (!*text)
					return;

				if (argc == 1)
					args = text;

				text = engine->com->Parse(text);
				if (!text)
					return;

				if (argc < MAX_ARGS)
				{
					strcpy_s(argv[argc], com_token);
					argc++;
				}
			}
		}


		Cmd(Engine* e) {
			engine = e;
			engine->cmd = this;
		}
	};
	class Cbuf {
	public:
		Engine* engine;
		Cbuf(Engine* e) {
			engine = e;
			engine->cbuf = this;
		}
		void AddText(const char* text)
		{
			int l;

			l = strlen(text);

			if (engine->cmd->text.cursize + l >= engine->cmd->text.maxsize)
			{
				SDL_Log("Cbuf_AddText: overflow\n");
				return;
			}

			engine->sz->Write(&engine->cmd->text, text, l);
		}


	};
	class Host {
	public:
		cvar_t name = { "hostname", "UNNAMED", CVAR_SERVERINFO };

		size_t		cacheCount = 0;
		hostcache_t cache[HOSTCACHESIZE];


		float  netinterval = 1.0 / HOST_NETITERVAL_FREQ;
		cvar_t maxfps = { "host_maxfps", "200", CVAR_ARCHIVE };	// johnfitz
		cvar_t timescale = { "host_timescale", "0", CVAR_NONE }; // johnfitz
		cvar_t framerate = { "host_framerate", "0", CVAR_NONE }; // set for slow motion

		client_t* client; // current client


		double frametime;

		double realtime;	// without any filtering or bounding
		double oldrealtime; // last frame run


		Engine* engine;

		jmp_buf abortserver;


		Host(Engine* e) {
			engine = e;
			engine->host = this;
		}

		void ShutdownServer(bool crash)
		{
			int		  i;
			int		  count;
			sizebuf_t buf;
			byte	  message[4];
			double	  start;

			if (!engine->sv.active)
				return;

			engine->sv.active = false;

			// stop all client sounds immediately
			if (engine->cl->s->state == ca_connected)
				engine->cl->Disconnect();

			// flush any pending messages - like the score!!!
			start = DoubleTime();
			do
			{
				count = 0;
				for (i = 0, client = engine->svs.clients; i < engine->svs.maxclients; i++, client++)
				{
					if (client->active && client->message.cursize && client->netconnection)
					{
						if (engine->net->CanSendMessage(client->netconnection))
						{
							engine->net->NET_SendMessage(client->netconnection, &client->message);
							engine->sz->Clear(&client->message);
						}
						else
						{
							engine->net->NET_GetMessage(client->netconnection);
							count++;
						}
					}
				}
				if ((DoubleTime() - start) > 3.0)
					break;
			} while (count);

			// make sure all the clients know we're disconnecting
			buf.data = message;
			buf.maxsize = 4;
			buf.cursize = 0;
			engine->msg->WriteByte(&buf, svc_disconnect);
			//count = NET_SendToAll(&buf, 5.0);
			if (count)
				SDL_Log("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

			//PR_SwitchQCVM(&sv.qcvm);
			//for (i = 0, client = engine->svs.clients; i < engine->svs.maxclients; i++, client++)
			//	if (client->active)
			//		engine->server->DropClient(crash);

			//qcvm->worldmodel = NULL;
			//PR_SwitchQCVM(NULL);

			//
			// clear structures
			//
			//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
			memset(engine->svs.clients, 0, engine->svs.maxclientslimit * sizeof(client_t));
		}



		void Error(const char* error, ...)
		{
			va_list			argptr;
			char			string[1024];
			static bool inerror = false;

			if (inerror)
				Error("Host_Error: recursively entered");
			inerror = true;

			//PR_SwitchQCVM(NULL);

			//SCR_EndLoadingPlaque(); // reenable screen updates

			va_start(argptr, error);
			q_vsnprintf(string, sizeof(string), error, argptr);
			va_end(argptr);
			SDL_Log("Host_Error: %s\n", string);

			/*if (cl.qcvm.extfuncs.CSQC_DrawHud && in_update_screen)
			{
				inerror = false;
				longjmp(screen_error, 1);
			}*/

			if (engine->sv.active)
				engine->host->ShutdownServer(false);

			if (engine->cl->s->state == ca_dedicated)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Host_Error: %s\n", string); // dedicated servers exit

			engine->cl->Disconnect();
			engine->cl->s->demonum = -1;
			engine->cl->state->intermission = 0; // johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

			inerror = false;

			longjmp(engine->host->abortserver, 1);
		}

		bool FilterTime(float time)
		{

			float max_fps; // johnfitz
			float min_frame_time;
			float delta_since_last_frame;

			realtime += time;
			delta_since_last_frame = realtime - oldrealtime;

			if (maxfps.value)
			{
				// johnfitz -- max fps cvar
				max_fps = CLAMP(10.0, maxfps.value, 1000.0);

				// Check if we still have more than 2ms till next frame and if so wait for "1ms"
				// E.g. Windows is not a real time OS and the sleeps can vary in length even with timeBeginPeriod(1)
				min_frame_time = 1.0f / max_fps;
				if ((min_frame_time - delta_since_last_frame) > (2.0f / 1000.0f))
					SDL_Delay(1);

				if (!engine->cl->s->timedemo && (delta_since_last_frame < min_frame_time))
					return false; // framerate is too high
				// johnfitz
			}

			frametime = delta_since_last_frame;
			oldrealtime = realtime;

			if (engine->cl->s->demoplayback && engine->cl->s->demospeed != 1.f && engine->cl->s->demospeed > 0.f)
				frametime *= engine->cl->s->demospeed;
			// johnfitz -- host_timescale is more intuitive than host_framerate
			else if (timescale.value > 0)
				frametime *= timescale.value;
			// johnfitz
			else if (framerate.value > 0)
				frametime = framerate.value;
			else if (maxfps.value)								  // don't allow really long or short frames
				frametime = CLAMP(0.0001, frametime, 0.1); // johnfitz -- use CLAMP

			return true;
		}

		void GetConsoleCommands()
		{
			const char* cmd;

			if (!engine->isDedicated)
				return; // no stdin necessary in graphical mode

			while (1)
			{
				cmd = engine->sys->ConsoleInput();
				if (!cmd)
					break;
				engine->cbuf->AddText(cmd);
			}
		}

		void Frame(double time) {

			double accumtime = 0;

			double before = DoubleTime();

			if (setjmp(abortserver)) {
				return;
			}

			engine->com->Rand();

			accumtime += netinterval ? CLAMP(0, time, 0.2) : 0; // for renderer/server isolation
			if (!FilterTime(time))
				return; // don't run too fast, or packets will flood out




			double after = DoubleTime();

			double delta = after - before;
			engine->ticks++;
		}
	};
	class SCR {
	public:
		Engine* engine;
		SCR(Engine* e) {
			engine = e;
			engine->scr = this;
		}
	};
	class SZ {
	public:
		Engine* engine;
		SZ(Engine* e) {
			engine = e;
			engine->sz = this;
		}

		void Clear(sizebuf_t* buf)
		{
			buf->cursize = 0;
			buf->overflowed = false;
		}

		void* GetSpace(sizebuf_t* buf, int length)
		{
			void* data;

			if (buf->cursize + length > buf->maxsize)
			{
				if (!buf->allowoverflow)
					engine->host->Error("SZ_GetSpace: overflow without allowoverflow set"); // ericw -- made Host_Error to be less annoying

				if (length > buf->maxsize)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"SZ_GetSpace: %i is > full buffer size", length);

				SDL_Log("SZ_GetSpace: overflow\n");
				Clear(buf);
				buf->overflowed = true;
			}

			data = buf->data + buf->cursize;
			buf->cursize += length;

			return data;
		}

		void Write(sizebuf_t* buf, const void* data, int length)
		{
			memcpy(GetSpace(buf, length), data, length);
		}
	};
	class SV {
	public:
		Engine* engine;
		bool active;
		SV(Engine* e) {
			engine = e;
			engine->server = this;
		}
		//void DropClient(bool crash)
		//{
		//	int		  saveSelf;
		//	int		  i;
		//	client_t* client;
		//	if (!crash)
		//	{
		//		// send any final messages (don't check for errors)
		//		if (engine->net->CanSendMessage(engine->host->client->netconnection))
		//		{
		//			engine->msg->WriteByte(&engine->host->client->message, svc_disconnect);
		//			engine->net->NET_SendMessage(engine->host->client->netconnection, &engine->host->client->message);
		//		}
		//		if (engine->host->client->edict && engine->host->client->spawned)
		//		{
		//			// call the prog function for removing a client
		//			// this will set the body to a dead frame, among other things
		//			//qcvm_t* oldvm = qcvm;
		//			//PR_SwitchQCVM(NULL);
		//			//PR_SwitchQCVM(&sv.qcvm);
		//			//saveSelf = pr_global_struct->self;
		//			//pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
		//			//PR_ExecuteProgram(pr_global_struct->ClientDisconnect);
		//			//pr_global_struct->self = saveSelf;
		//			//PR_SwitchQCVM(NULL);
		//			//PR_SwitchQCVM(oldvm);
		//		}
		//		SDL_Log("Client %s removed\n", engine->host->client->name);
		//	}
		//	// break the net connection
		//	engine->net->Close(engine->host->client->netconnection);
		//	engine->host->client->netconnection = NULL;
		//	//SVFTE_DestroyFrames(engine->host->client); // release any delta state
		//	// free the client (the body stays around)
		//	engine->host->client->active = false;
		//	engine->host->client->name[0] = 0;
		//	engine->host->client->old_frags = -999999;
		//	engine->net->activeconnections--;
		//	// send notification to all clients
		//	for (i = 0, client = engine->svs.clients; i < engine->svs.maxclients; i++, client++)
		//	{
		//		if (!client->knowntoqc)
		//			continue;
		//		engine->msg->WriteByte(&client->message, svc_updatename);
		//		engine->msg->WriteByte(&client->message, engine->host->client - engine->svs.clients);
		//		engine->msg->WriteString(&client->message, "");
		//		engine->msg->WriteByte(&client->message, svc_updatecolors);
		//		engine->msg->WriteByte(&client->message, engine->host->client - engine->svs.clients);
		//		engine->msg->WriteByte(&client->message, 0);
		//		engine->msg->WriteByte(&client->message, svc_updatefrags);
		//		engine->msg->WriteByte(&client->message, engine->host->client - engine->svs.clients);
		//		engine->msg->WriteShort(&client->message, 0);
		//	}
		//}


	};
	class NET {
	public:

		char my_ipx_address[NET_NAMELEN];
		char my_ipv4_address[NET_NAMELEN];
		char my_ipv6_address[NET_NAMELEN];

		int landriverlevel;

		cvar_t messagetimeout = { "net_messagetimeout", "300", CVAR_NONE };
		cvar_t connecttimeout = { "net_connecttimeout", "10", CVAR_NONE }; // this might be a little brief, but we don't have a way to protect against smurf attacks.
		cvar_t hostname = { "hostname", "UNNAMED", CVAR_SERVERINFO };


		std::vector<net_landriver_t> landrivers {
			
		#ifdef IPPROTO_IPV6
			{"Winsock IPv6",
			 false,
			 0,
			 WINIPv6_Init,
			 WINIPv6_Shutdown,
			 WINIPv6_Listen,
			 WINIPv6_GetAddresses,
			 WINIPv6_OpenSocket,
			 WINS_CloseSocket,
			 WINS_Connect,
			 WINIPv6_CheckNewConnections,
			 WINS_Read,
			 WINS_Write,
			 WINIPv6_Broadcast,
			 WINS_AddrToString,
			 WINIPv6_StringToAddr,
			 WINS_GetSocketAddr,
			 WINIPv6_GetNameFromAddr,
			 WINIPv6_GetAddrFromName,
			 WINS_AddrCompare,
			 WINS_GetSocketPort,
			 WINS_SetSocketPort},
		#endif
			/*{"Winsock IPX", // OUTDATED GARBAGE
			 false,
			 0,
			 WIPX_Init,
			 WIPX_Shutdown,
			 WIPX_Listen,
			 WIPX_GetAddresses,
			 WIPX_OpenSocket,
			 WIPX_CloseSocket,
			 WIPX_Connect,
			 WIPX_CheckNewConnections,
			 WIPX_Read,
			 WIPX_Write,
			 WIPX_Broadcast,
			 WIPX_AddrToString,
			 WIPX_StringToAddr,
			 WIPX_GetSocketAddr,
			 WIPX_GetNameFromAddr,
			 WIPX_GetAddrFromName,
			 WIPX_AddrCompare,
			 WIPX_GetSocketPort,
			 WIPX_SetSocketPort}*/ 
		
		};

		int	   numlandrivers;

		qsocket_t* activeSockets = NULL;
		qsocket_t* freeSockets = NULL;
		int		   numsockets = 0;


		
		//net_driver_t* drivers = NULL;


		Engine* engine;
		double time;

		//qsocket_t* activeSockets = NULL;
		//qsocket_t* freeSockets = NULL;
		int		   net_numsockets = 0;

		bool ipxAvailable = false;
		bool ipv4Available = false;
		bool ipv6Available = false;

		int net_hostport;
		int DEFAULTnet_hostport = 26000;

		//char my_ipx_address[NET_NAMELEN];
		//char my_ipv4_address[NET_NAMELEN];
		//char my_ipv6_address[NET_NAMELEN];

		bool listening = false;

		bool		  slistInProgress = false;
		bool		  slist_silent = false;
		enum slistScope_e slist_scope = SLIST_LOOP;
		static double	  slistStartTime;
		static double	  slistActiveTime;
		static int		  slistLastShown;

		static void			 Slist_Send(void*) {};
		static void			 Slist_Poll(void*) {};
		PollProcedure slistSendProcedure = { NULL, 0.0, Slist_Send };
		PollProcedure slistPollProcedure = { NULL, 0.0, Slist_Poll };

		sizebuf_t message;
		int		  activeconnections = 0;

		int messagesSent = 0;
		int messagesReceived = 0;
		int unreliableMessagesSent = 0;
		int unreliableMessagesReceived = 0;

		int		driverlevel;


		NET(Engine* e) {
			engine = e;
			engine->net = this;

			//landrivers.insert(landrivers.begin(), net_landriver_t{ "Winsock TCPIP",
			// false,
			// 0,
			// Engine::WINIP::WINIPv4_Init,
			// Engine::WINIP::WINIPv4_Shutdown,
			// Engine::WINIP::WINIPv4_Listen,
			// Engine::WINIP::WINIPv4_GetAddresses,
			// Engine::WINIP::WINIPv4_OpenSocket,
			// Engine::WINIP::WINS_CloseSocket,
			// Engine::WINIP::WINS_Connect,
			// Engine::WINIP::WINIPv4_CheckNewConnections,
			// Engine::WINIP::WINS_Read,
			// Engine::WINIP::WINS_Write,
			// Engine::WINIP::WINIPv4_Broadcast,
			// Engine::WINIP::WINS_AddrToString,
			// Engine::WINIP::WINIPv4_StringToAddr,
			// Engine::WINIP::WINS_GetSocketAddr,
			// Engine::WINIP::WINIPv4_GetNameFromAddr,
			// Engine::WINIP::WINIPv4_GetAddrFromName,
			// Engine::WINIP::WINS_AddrCompare,
			// Engine::WINIP::WINS_GetSocketPort,
			// Engine::WINIP::WINS_SetSocketPort });
		}

		double SetNetTime(void)
		{
			time = DoubleTime();
			return time;
		}

		qsocket_t* NewQSocket(void)
		{
			qsocket_t* sock;

			if (engine->net->freeSockets == NULL)
				return NULL;

			if (activeconnections >= engine->svs.maxclients)
				return NULL;

			// get one from free list
			sock = engine->net->freeSockets;
			engine->net->freeSockets = sock->next;

			// add it to active list
			sock->next = engine->net->activeSockets;
			engine->net->activeSockets = sock;

			sock->isvirtual = false;
			sock->disconnected = false;
			sock->connecttime = time;
			strcpy_s(sock->trueaddress, "UNSET ADDRESS");
			strcpy_s(sock->maskedaddress, "UNSET ADDRESS");
			sock->driver = driverlevel;
			sock->socket = 0;
			sock->driverdata = NULL;
			sock->canSend = true;
			sock->sendNext = false;
			sock->lastMessageTime = time;
			sock->ackSequence = 0;
			sock->sendSequence = 0;
			sock->unreliableSendSequence = 0;
			sock->sendMessageLength = 0;
			sock->receiveSequence = 0;
			sock->unreliableReceiveSequence = 0;
			sock->receiveMessageLength = 0;
			sock->pending_max_datagram = 1024;
			sock->proquake_angle_hack = false;

			return sock;
		}

		void FreeQSocket(qsocket_t* sock)
		{
			qsocket_t* s;

			// remove it from active list
			if (sock == engine->net->activeSockets)
				engine->net->activeSockets = engine->net->activeSockets->next;
			else
			{
				for (s = engine->net->activeSockets; s; s = s->next)
				{
					if (s->next == sock)
					{
						s->next = sock->next;
						break;
					}
				}

				if (!s)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"NET_FreeQSocket: not active");
			}

			// add it to free list
			sock->next = engine->net->freeSockets;
			engine->net->freeSockets = sock;
			sock->disconnected = true;
		}


		void Close(qsocket_t* sock)
		{
			if (!sock)
				return;

			if (sock->disconnected)
				return;

			SetNetTime();

			// call the driver_Close function
			//sfunc.Close(sock);

			FreeQSocket(sock);
		}


		bool CanSendMessage(qsocket_t* sock)
		{
			if (!sock)
				return false;

			if (sock->disconnected)
				return false;

			SetNetTime();

			//return sfunc.CanSendMessage(sock);
		}

		int NET_GetMessage(qsocket_t* sock)
		{
			int ret;

			if (!sock)
				return -1;

			if (sock->disconnected)
			{
				SDL_Log("NET_GetMessage: disconnected socket\n");
				return -1;
			}

			SetNetTime();

			//ret = sfunc.QGetMessage(sock);

			// see if this connection has timed out
			if (ret == 0 && !IS_LOOP_DRIVER(sock->driver))
			{
				if (time - sock->lastMessageTime > messagetimeout.value)
				{
					Close(sock);
					return -1;
				}
			}

			if (ret > 0)
			{
				if (!IS_LOOP_DRIVER(sock->driver))
				{
					sock->lastMessageTime = time;
					if (ret == 1)
						messagesReceived++;
					else if (ret == 2)
						unreliableMessagesReceived++;
				}
			}

			return ret;
		}


		int NET_SendMessage(qsocket_t* sock, sizebuf_t* data)
		{
			int r;

			if (!sock)
				return -1;

			if (sock->disconnected)
			{
				SDL_Log("NET_SendMessage: disconnected socket\n");
				return -1;
			}

			SetNetTime();
			//r = sfunc.QSendMessage(sock, data);
			if (r == 1 && !IS_LOOP_DRIVER(sock->driver))
				messagesSent++;

			return r;
		}

		int SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
		{
			int r;

			if (!sock)
				return -1;

			if (sock->disconnected)
			{
				SDL_Log("NET_SendMessage: disconnected socket\n");
				return -1;
			}

			SetNetTime();
			//r = sfunc.SendUnreliableMessage(sock, data);
			if (r == 1 && !IS_LOOP_DRIVER(sock->driver))
				unreliableMessagesSent++;

			return r;
		}


	};
	class MSG {
	public:
		Engine* engine;
		MSG(Engine* e) {
			engine = e;
			engine->msg = this;
		}

		int		 readcount;
		bool	badread;

		void BeginReading(void)
		{
			engine->msg->readcount = 0;
			engine->msg->badread = false;
		}

		int ReadLong(void)
		{
			uint32_t c;

			if (engine->msg->readcount + 4 > engine->net->message.cursize)
			{
				engine->msg->badread = true;
				return -1;
			}

			c = (uint32_t)engine->net->message.data[engine->msg->readcount] + ((uint32_t)(engine->net->message.data[engine->msg->readcount + 1]) << 8) +
				((uint32_t)(engine->net->message.data[engine->msg->readcount + 2]) << 16) + ((uint32_t)(engine->net->message.data[engine->msg->readcount + 3]) << 24);

			engine->msg->readcount += 4;

			return c;
		}



		const char* ReadString(void)
		{
			static char string[2048];
			int			c;
			size_t		l;

			l = 0;
			do
			{
				c = ReadByte();
				if (c == -1 || c == 0)
					break;
				string[l] = c;
				l++;
			} while (l < sizeof(string) - 1);

			string[l] = 0;

			return string;
		}



		int ReadByte(void)
		{
			int c;

			if (engine->msg->readcount + 1 > engine->net->message.cursize)
			{
				engine->msg->badread = true;
				return -1;
			}

			c = (unsigned char)engine->net->message.data[engine->msg->readcount];
			engine->msg->readcount++;

			return c;
		}

		void WriteLong(sizebuf_t* sb, int c)
		{
			byte* buf;

			buf = (byte*)engine->sz->GetSpace(sb, 4);
			buf[0] = c & 0xff;
			buf[1] = (c >> 8) & 0xff;
			buf[2] = (c >> 16) & 0xff;
			buf[3] = c >> 24;
		}

		void WriteString(sizebuf_t* sb, const char* s)
		{
			if (!s)
				engine->sz->Write(sb, "", 1);
			else
				engine->sz->Write(sb, s, strlen(s) + 1);
		}

		void WriteShort(sizebuf_t* sb, int c)
		{
			byte* buf;

#ifdef PARANOID
			if (c < ((short)0x8000) || c >(short)0x7fff)
				Sys_Error("MSG_WriteShort: range error");
#endif

			buf = (byte*)engine->sz->GetSpace(sb, 2);
			buf[0] = c & 0xff;
			buf[1] = c >> 8;
		}


		void WriteByte(sizebuf_t* sb, int c)
		{
			byte* buf;

#ifdef PARANOID
			if (c < 0 || c > 255)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"engine->msg->WriteByte: range error");
#endif

			buf = (byte*)engine->sz->GetSpace(sb, 1);
			buf[0] = c;
		}
	};

	VID* vid = nullptr;
	GL* gl = nullptr;
	COM* com = nullptr;
	Tasks* tasks = nullptr;
	Host* host = nullptr;
	REN* ren = nullptr;
	CL* cl = nullptr;
	SCR* scr = nullptr;
	Sys* sys = nullptr;
	Cbuf* cbuf = nullptr;
	Cmd* cmd = nullptr;
	SZ* sz = nullptr;
	SV* server = nullptr;
	NET* net = nullptr;
	//Loop* loop = nullptr;
	MSG* msg = nullptr;
	//Datagram* datagram = nullptr;

	int argc; char** argv;

	Engine(int ct, char* var[]) {	

		max_thread_stack_alloc_size = MAX_STACK_ALLOC_SIZE;

		argc = ct; argv = var;

		tasks = new Tasks(this);
		com = new COM(this);
		host = new Host(this);

		vid = new VID(this);
		gl = new GL(this);

		scr = new SCR(this);
		sys = new Sys(this);
		cl = new CL(this);
		cbuf = new Cbuf(this);
		cmd = new Cmd(this);
		sz = new SZ(this);
		server = new SV(this);
		net = new NET(this);
		//loop = new Loop(this);
		msg = new MSG(this);
		//datagram = new Datagram(this);

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