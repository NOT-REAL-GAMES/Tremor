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

	bool isDedicated;

	server_t		sv;
	server_static_t svs;


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

	class Loop {
	public:
		Engine* engine;
		bool	  localconnectpending = false;
		qsocket_t* loop_client = NULL;
		qsocket_t* loop_server = NULL;

		Loop(Engine* e) { engine = e; engine->loop = this; }

		static int Loop_Init(Engine engine)
		{
			if (engine.cl->s->state == ca_dedicated)
				return -1;
			return 0;
		}

		static void Loop_Shutdown(Engine engine) {}

		static void Loop_Listen(bool state, Engine engine) {}

		static bool Loop_SearchForHosts(bool xmit, Engine engine)
		{
			if (!engine.sv.active)
				return false;

			engine.host->cacheCount = 1;
			if (strcmp(engine.host->name.string, "UNNAMED") == 0)
				strcpy_s(engine.host->cache[0].name, "local");
			else
				strcpy_s(engine.host->cache[0].name, engine.host->name.string);
			strcpy_s(engine.host->cache[0].map, engine.sv.name);
			engine.host->cache[0].users = engine.net->activeconnections;
			engine.host->cache[0].maxusers = engine.svs.maxclients;
			engine.host->cache[0].driver = engine.net->driverlevel;
			strcpy_s(engine.host->cache[0].cname, "local");
			return false;
		}

		static qsocket_t* Loop_Connect(const char* host, Engine engine)
		{
			if (strcmp(host, "local") != 0)
				return NULL;

			engine.loop->localconnectpending = true;

			if (!engine.loop->loop_client)
			{
				if ((engine.loop->loop_client = engine.net->NewQSocket()) == NULL)
				{
					SDL_Log("Loop_Connect: no qsocket available\n");
					return NULL;
				}
				strcpy_s(engine.loop->loop_client->trueaddress, "localhost");
				strcpy_s(engine.loop->loop_client->maskedaddress, "localhost");
			}
			engine.loop->loop_client->receiveMessageLength = 0;
			engine.loop->loop_client->sendMessageLength = 0;
			engine.loop->loop_client->canSend = true;

			if (!engine.loop->loop_server)
			{
				if ((engine.loop->loop_server = engine.net->NewQSocket()) == NULL)
				{
					SDL_Log("Loop_Connect: no qsocket available\n");
					return NULL;
				}
				strcpy_s(engine.loop->loop_server->trueaddress, "LOCAL");
				strcpy_s(engine.loop->loop_server->maskedaddress, "LOCAL");
			}
			engine.loop->loop_server->receiveMessageLength = 0;
			engine.loop->loop_server->sendMessageLength = 0;
			engine.loop->loop_server->canSend = true;

			engine.loop->loop_client->driverdata = (void*)engine.loop->loop_server;
			engine.loop->loop_server->driverdata = (void*)engine.loop->loop_client;

			engine.loop->loop_client->proquake_angle_hack = engine.loop->loop_server->proquake_angle_hack = true;

			return engine.loop->loop_client;
		}

		qsocket_t* Loop_CheckNewConnections(void)
		{
			if (!localconnectpending)
				return NULL;

			localconnectpending = false;
			loop_server->sendMessageLength = 0;
			loop_server->receiveMessageLength = 0;
			loop_server->canSend = true;
			loop_client->sendMessageLength = 0;
			loop_client->receiveMessageLength = 0;
			loop_client->canSend = true;
			return loop_server;
		}

		static int IntAlign(int value)
		{
			return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
		}

		int Loop_GetMessage(qsocket_t* sock)
		{
			int ret;
			int length;

			if (sock->receiveMessageLength == 0)
				return 0;

			ret = sock->receiveMessage[0];
			length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
			// alignment byte skipped here
			engine->sz->Clear(&engine->net->message);
			if (ret == 2)
			{ // unreliables have sequences that we (now) care about so that clients can ack them.
				sock->unreliableReceiveSequence =
					sock->receiveMessage[4] | (sock->receiveMessage[5] << 8) | (sock->receiveMessage[6] << 16) | (sock->receiveMessage[7] << 24);
				sock->unreliableReceiveSequence++;
				engine->sz->Write(&engine->net->message, &sock->receiveMessage[8], length);
				length = IntAlign(length + 8);
			}
			else
			{ // reliable
				engine->sz->Write(&engine->net->message, &sock->receiveMessage[4], length);
				length = IntAlign(length + 4);
			}

			sock->receiveMessageLength -= length;

			if (sock->receiveMessageLength)
				memmove(sock->receiveMessage, &sock->receiveMessage[length], sock->receiveMessageLength);

			if (sock->driverdata && ret == 1)
				((qsocket_t*)sock->driverdata)->canSend = true;

			return ret;
		}

		qsocket_t* Loop_GetAnyMessage(void)
		{
			if (loop_server)
			{
				if (Loop_GetMessage(loop_server) > 0)
					return loop_server;
			}
			return NULL;
		}

		int Loop_SendMessage(qsocket_t* sock, sizebuf_t* data)
		{
			byte* buffer;
			int* bufferLength;

			if (!sock->driverdata)
				return -1;

			bufferLength = &((qsocket_t*)sock->driverdata)->receiveMessageLength;

			if ((*bufferLength + data->cursize + NET_LOOPBACKHEADERSIZE) > NET_MAXMESSAGE * NET_LOOPBACKBUFFERS + NET_LOOPBACKHEADERSIZE)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Loop_SendMessage: overflow");

			buffer = ((qsocket_t*)sock->driverdata)->receiveMessage + *bufferLength;

			// message type
			*buffer++ = 1;

			// length
			*buffer++ = data->cursize & 0xff;
			*buffer++ = data->cursize >> 8;

			// align
			buffer++;

			// message
			memcpy(buffer, data->data, data->cursize);
			*bufferLength = IntAlign(*bufferLength + data->cursize + 4);

			sock->canSend = false;
			return 1;
		}

		int Loop_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
		{
			byte* buffer;
			int* bufferLength;
			int	  sequence = sock->unreliableSendSequence++;

			if (!sock->driverdata)
				return -1;

			bufferLength = &((qsocket_t*)sock->driverdata)->receiveMessageLength;

			// always leave one buffer for reliable messages
			if ((*bufferLength + data->cursize + NET_LOOPBACKHEADERSIZE) > NET_MAXMESSAGE * (NET_LOOPBACKBUFFERS - 1))
				return 0;

			buffer = ((qsocket_t*)sock->driverdata)->receiveMessage + *bufferLength;

			// message type
			*buffer++ = 2;

			// length
			*buffer++ = data->cursize & 0xff;
			*buffer++ = data->cursize >> 8;

			// align
			buffer++;

			*buffer++ = (sequence >> 0) & 0xff;
			*buffer++ = (sequence >> 8) & 0xff;
			*buffer++ = (sequence >> 16) & 0xff;
			*buffer++ = (sequence >> 24) & 0xff;

			// message
			memcpy(buffer, data->data, data->cursize);
			*bufferLength = IntAlign(*bufferLength + data->cursize + 8);
			return 1;
		}

		bool Loop_CanSendMessage(qsocket_t* sock)
		{
			if (!sock->driverdata)
				return false;
			return sock->canSend;
		}

		bool Loop_CanSendUnreliableMessage(qsocket_t* sock)
		{
			return true;
		}

		void Loop_Close(qsocket_t* sock)
		{
			if (sock->driverdata)
				((qsocket_t*)sock->driverdata)->driverdata = NULL;
			sock->receiveMessageLength = 0;
			sock->sendMessageLength = 0;
			sock->canSend = true;
			if (sock == loop_client)
				loop_client = NULL;
			else
				loop_server = NULL;
		}

	};
	class Datagram {
	public:

		int packetsSent = 0;
		int packetsReSent = 0;
		int packetsReceived = 0;
		int receivedDuplicateCount = 0;
		int shortPacketCount = 0;
		int droppedDatagrams;
		int messagesSent = 0;
		int messagesReceived = 0;
		int unreliableMessagesSent = 0;
		int unreliableMessagesReceived = 0;
		Engine* engine;
		Datagram(Engine* e) { engine = e; engine->datagram = this; }
		int Datagram_SendMessage(qsocket_t* sock, sizebuf_t* data)
		{
			unsigned int packetLen;
			unsigned int dataLen;
			unsigned int eom;

#ifdef DEBUG
			if (data->cursize == 0)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Datagram_SendMessage: zero length message");

			if (data->cursize > NET_MAXMESSAGE)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Datagram_SendMessage: message too big: %u", data->cursize);

			if (sock->canSend == false)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"SendMessage: called with canSend == false");
#endif

			memcpy(sock->sendMessage, data->data, data->cursize);
			sock->sendMessageLength = data->cursize;

			sock->max_datagram = sock->pending_max_datagram; // this can apply only at the start of a reliable, to avoid issues with acks if its resized later.

			if (data->cursize <= sock->max_datagram)
			{
				dataLen = data->cursize;
				eom = NETFLAG_EOM;
			}
			else
			{
				dataLen = sock->max_datagram;
				eom = 0;
			}
			packetLen = NET_HEADERSIZE + dataLen;

			packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
			packetBuffer.sequence = BigLong(sock->sendSequence++);
			memcpy(packetBuffer.data, sock->sendMessage, dataLen);

			sock->canSend = false;

			if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1)
				return -1;

			sock->lastSendTime = engine->net->time;
			packetsSent++;
			return 1;
		}

		int SendMessageNext(qsocket_t* sock)
		{
			unsigned int packetLen;
			unsigned int dataLen;
			unsigned int eom;

			if (sock->sendMessageLength <= sock->max_datagram)
			{
				dataLen = sock->sendMessageLength;
				eom = NETFLAG_EOM;
			}
			else
			{
				dataLen = sock->max_datagram;
				eom = 0;
			}
			packetLen = NET_HEADERSIZE + dataLen;

			packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
			packetBuffer.sequence = BigLong(sock->sendSequence++);
			memcpy(packetBuffer.data, sock->sendMessage, dataLen);

			sock->sendNext = false;

			if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1)
				return -1;

			sock->lastSendTime = engine->net->time;
			packetsSent++;
			return 1;
		}

		int ReSendMessage(qsocket_t* sock)
		{
			unsigned int packetLen;
			unsigned int dataLen;
			unsigned int eom;

			if (sock->sendMessageLength <= sock->max_datagram)
			{
				dataLen = sock->sendMessageLength;
				eom = NETFLAG_EOM;
			}
			else
			{
				dataLen = sock->max_datagram;
				eom = 0;
			}
			packetLen = NET_HEADERSIZE + dataLen;

			packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
			packetBuffer.sequence = BigLong(sock->sendSequence - 1);
			memcpy(packetBuffer.data, sock->sendMessage, dataLen);

			if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1)
				return -1;

			sock->lastSendTime = engine->net->time;
			packetsReSent++;
			return 1;
		}

		bool Datagram_CanSendMessage(qsocket_t* sock)
		{
			if (sock->sendNext)
				SendMessageNext(sock);

			return sock->canSend;
		}

		bool Datagram_CanSendUnreliableMessage(qsocket_t* sock)
		{
			return true;
		}

		int Datagram_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
		{
			int packetLen;

#ifdef DEBUG
			if (data->cursize == 0)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Datagram_SendUnreliableMessage: zero length message");

			if (data->cursize > MAX_DATAGRAM)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Datagram_SendUnreliableMessage: message too big: %u", data->cursize);
#endif

			packetLen = NET_HEADERSIZE + data->cursize;

			packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
			packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
			memcpy(packetBuffer.data, data->data, data->cursize);

			if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1)
				return -1;

			packetsSent++;
			return 1;
		}

		void _Datagram_ServerControlPacket(sys_socket_t acceptsock, struct qsockaddr* clientaddr, byte* data, unsigned int length);

		bool Datagram_ProcessPacket(unsigned int length, qsocket_t* sock)
		{
			unsigned int flags;
			unsigned int sequence;
			unsigned int count;

			if (length < NET_HEADERSIZE)
			{
				shortPacketCount++;
				return false;
			}

			length = BigLong(packetBuffer.length);
			flags = length & (~NETFLAG_LENGTH_MASK);
			length &= NETFLAG_LENGTH_MASK;

			if (flags & NETFLAG_CTL)
				return false; // should only be for OOB packets.

			sequence = BigLong(packetBuffer.sequence);
			packetsReceived++;

			if (flags & NETFLAG_UNRELIABLE)
			{
				if (sequence < sock->unreliableReceiveSequence)
				{
					SDL_Log("Got a stale datagram\n");
					return false;
				}
				if (sequence != sock->unreliableReceiveSequence)
				{
					count = sequence - sock->unreliableReceiveSequence;
					droppedDatagrams += count;
					SDL_Log("Dropped %u datagram(s)\n", count);
				}
				sock->unreliableReceiveSequence = sequence + 1;

				length -= NET_HEADERSIZE;

				if (length > (unsigned int)engine->net->message.maxsize)
				{ // is this even possible? maybe it will be in the future! either way, no sys_errors please.
					SDL_Log("Over-sized unreliable\n");
					return true;
				}
				engine->sz->Clear(&engine->net->message);
				engine->sz->Write(&engine->net->message, packetBuffer.data, length);

				unreliableMessagesReceived++;
				return true; // parse the unreliable
			}

			if (flags & NETFLAG_ACK)
			{
				if (sequence != (sock->sendSequence - 1))
				{
					SDL_Log("Stale ACK received\n");
					return false;
				}
				if (sequence == sock->ackSequence)
				{
					sock->ackSequence++;
					if (sock->ackSequence != sock->sendSequence)
						SDL_Log("ack sequencing error\n");
				}
				else
				{
					SDL_Log("Duplicate ACK received\n");
					return false;
				}
				sock->sendMessageLength -= sock->max_datagram;
				if (sock->sendMessageLength > 0)
				{
					memmove(sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
					sock->sendNext = true;
				}
				else
				{
					sock->sendMessageLength = 0;
					sock->canSend = true;
				}
				return false;
			}

			if (flags & NETFLAG_DATA)
			{
				packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
				packetBuffer.sequence = BigLong(sequence);
				sfunc.Write(sock->socket, (byte*)&packetBuffer, NET_HEADERSIZE, &sock->addr);

				if (sequence != sock->receiveSequence)
				{
					receivedDuplicateCount++;
					return false;
				}
				sock->receiveSequence++;

				length -= NET_HEADERSIZE;

				if (flags & NETFLAG_EOM)
				{
					if (sock->receiveMessageLength + length > (unsigned int)engine->net->message.maxsize)
					{
						SDL_Log("Over-sized reliable\n");
						return true;
					}
					engine->sz->Clear(&engine->net->message);
					engine->sz->Write(&engine->net->message, sock->receiveMessage, sock->receiveMessageLength);
					engine->sz->Write(&engine->net->message, packetBuffer.data, length);
					sock->receiveMessageLength = 0;

					messagesReceived++;
					return true; // parse this reliable!
				}

				if (sock->receiveMessageLength + length > sizeof(sock->receiveMessage))
				{
					SDL_Log("Over-sized reliable\n");
					return true;
				}
				memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
				sock->receiveMessageLength += length;
				return false; // still watiting for the eom
			}
			// unknown flags
			SDL_Log("Unknown packet flags\n");
			return false;
		}

		qsocket_t* Datagram_GetAnyMessage(void)
		{
			qsocket_t* s;
			struct qsockaddr addr;
			int				 length;
			for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
			{
				sys_socket_t sock;
				if (!dfunc.initialized)
					continue;
				sock = dfunc.listeningSock;
				if (sock == INVALID_SOCKET)
					continue;

				while (1)
				{
					length = dfunc.Read(sock, (byte*)&packetBuffer, NET_DATAGRAMSIZE, &addr);
					if (length == -1 || !length)
					{
						// no more packets, move on to the next.
						break;
					}

					if (length < 4)
						continue;
					if (BigLong(packetBuffer.length) & NETFLAG_CTL)
					{
						_Datagram_ServerControlPacket(sock, &addr, (byte*)&packetBuffer, length);
						continue;
					}

					// figure out which qsocket it was for
					for (s = engine->net->activeSockets; s; s = s->next)
					{
						if (s->driver != net_driverlevel)
							continue;
						if (s->disconnected)
							continue;
						if (!s->isvirtual)
							continue;
						if (dfunc.AddrCompare(&addr, &s->addr) == 0)
						{
							// okay, looks like this is us. try to process it, and if there's new data
							if (Datagram_ProcessPacket(length, s))
							{
								s->lastMessageTime = engine->net->time;
								return s; // the server needs to parse that packet.
							}
						}
					}
					// stray packet... ignore it and just try the next
				}
			}
			for (s = engine->net->activeSockets; s; s = s->next)
			{
				if (s->driver != net_driverlevel)
					continue;
				if (!s->isvirtual)
					continue;

				if (s->sendNext)
					SendMessageNext(s);
				if (!s->canSend)
					if ((engine->net->time - s->lastSendTime) > 1.0)
						ReSendMessage(s);

				if (engine->net->time - s->lastMessageTime > ((!s->ackSequence) ? engine->net->connecttimeout.value : engine->net->messagetimeout.value))
				{ // timed out, kick them
					// FIXME: add a proper challenge rather than assuming spoofers won't fake acks
					int i;
					for (i = 0; i < engine->svs.maxclients; i++)
					{
						if (engine->svs.clients[i].netconnection == s)
						{
							engine->host->client = &engine->svs.clients[i];
							engine->server->DropClient(false);
							break;
						}
					}
				}
			}

			return NULL;
		}

		int Datagram_GetMessage(qsocket_t* sock)
		{
			unsigned int	 length;
			unsigned int	 flags;
			int				 ret = 0;
			struct qsockaddr readaddr;
			unsigned int	 sequence;
			unsigned int	 count;

			if (!sock->canSend)
				if ((engine->net->time - sock->lastSendTime) > 1.0)
					ReSendMessage(sock);

			while (1)
			{
				length = (unsigned int)sfunc.Read(sock->socket, (byte*)&packetBuffer, NET_DATAGRAMSIZE, &readaddr);

				//	if ((rand() & 255) > 220)
				//		continue;

				if (length == 0)
					break;

				if (length == (unsigned int)-1)
				{
					SDL_Log("Read error\n");
					return -1;
				}

				if (sfunc.AddrCompare(&readaddr, &sock->addr) != 0)
				{
					SDL_Log("Stray/Forged packet received\n");
					SDL_Log("Expected: %s\n", sfunc.AddrToString(&sock->addr, false));
					SDL_Log("Received: %s\n", sfunc.AddrToString(&readaddr, false));
					continue;
				}

				if (length < NET_HEADERSIZE)
				{
					shortPacketCount++;
					continue;
				}

				length = BigLong(packetBuffer.length);
				flags = length & (~NETFLAG_LENGTH_MASK);
				length &= NETFLAG_LENGTH_MASK;

				if (flags & NETFLAG_CTL)
					continue;

				sequence = BigLong(packetBuffer.sequence);
				packetsReceived++;

				if (flags & NETFLAG_UNRELIABLE)
				{
					if (sequence < sock->unreliableReceiveSequence)
					{
						SDL_Log("Got a stale datagram\n");
						ret = 0;
						break;
					}
					if (sequence != sock->unreliableReceiveSequence)
					{
						count = sequence - sock->unreliableReceiveSequence;
						droppedDatagrams += count;
						SDL_Log("Dropped %u datagram(s)\n", count);
					}
					sock->unreliableReceiveSequence = sequence + 1;

					length -= NET_HEADERSIZE;

					engine->sz->Clear(&engine->net->message);
					engine->sz->Write(&engine->net->message, packetBuffer.data, length);

					ret = 2;
					break;
				}

				if (flags & NETFLAG_ACK)
				{
					if (sequence != (sock->sendSequence - 1))
					{
						SDL_Log("Stale ACK received\n");
						continue;
					}
					if (sequence == sock->ackSequence)
					{
						sock->ackSequence++;
						if (sock->ackSequence != sock->sendSequence)
							SDL_Log("ack sequencing error\n");
					}
					else
					{
						SDL_Log("Duplicate ACK received\n");
						continue;
					}
					sock->sendMessageLength -= sock->max_datagram;
					if (sock->sendMessageLength > 0)
					{
						memmove(sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
						sock->sendNext = true;
					}
					else
					{
						sock->sendMessageLength = 0;
						sock->canSend = true;
					}
					continue;
				}

				if (flags & NETFLAG_DATA)
				{
					packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
					packetBuffer.sequence = BigLong(sequence);
					sfunc.Write(sock->socket, (byte*)&packetBuffer, NET_HEADERSIZE, &readaddr);

					if (sequence != sock->receiveSequence)
					{
						receivedDuplicateCount++;
						continue;
					}
					sock->receiveSequence++;

					length -= NET_HEADERSIZE;

					if (flags & NETFLAG_EOM)
					{
						if (sock->receiveMessageLength + length > (unsigned int)engine->net->message.maxsize)
						{
							SDL_Log("Over-sized reliable\n");
							return -1;
						}
						engine->sz->Clear(&engine->net->message);
						engine->sz->Write(&engine->net->message, sock->receiveMessage, sock->receiveMessageLength);
						engine->sz->Write(&engine->net->message, packetBuffer.data, length);
						sock->receiveMessageLength = 0;

						ret = 1;
						break;
					}

					if (sock->receiveMessageLength + length > sizeof(sock->receiveMessage))
					{
						SDL_Log("Over-sized reliable\n");
						return -1;
					}
					memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
					sock->receiveMessageLength += length;
					continue;
				}
			}

			if (sock->sendNext)
				SendMessageNext(sock);

			return ret;
		}

		void PrintStats(qsocket_t* s)
		{
			SDL_Log("canSend = %4u   \n", s->canSend);
			SDL_Log("sendSeq = %4u   ", s->sendSequence);
			SDL_Log("recvSeq = %4u   \n", s->receiveSequence);
			SDL_Log("\n");
		}

		void NET_Stats_f(void)
		{
			qsocket_t* s;

			if (Cmd_Argc() == 1)
			{
				SDL_Log("unreliable messages sent   = %i\n", unreliableMessagesSent);
				SDL_Log("unreliable messages recv   = %i\n", unreliableMessagesReceived);
				SDL_Log("reliable messages sent     = %i\n", messagesSent);
				SDL_Log("reliable messages received = %i\n", messagesReceived);
				SDL_Log("packetsSent                = %i\n", packetsSent);
				SDL_Log("packetsReSent              = %i\n", packetsReSent);
				SDL_Log("packetsReceived            = %i\n", packetsReceived);
				SDL_Log("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
				SDL_Log("shortPacketCount           = %i\n", shortPacketCount);
				SDL_Log("droppedDatagrams           = %i\n", droppedDatagrams);
			}
			else if (strcmp(Cmd_Argv(1), "*") == 0)
			{
				for (s = engine->net->activeSockets; s; s = s->next)
					PrintStats(s);
				for (s = net_freeSockets; s; s = s->next)
					PrintStats(s);
			}
			else
			{
				for (s = engine->net->activeSockets; s; s = s->next)
				{
					if (q_strcasecmp(Cmd_Argv(1), s->trueaddress) == 0 || q_strcasecmp(Cmd_Argv(1), s->maskedaddress) == 0)
						break;
				}

				if (s == NULL)
				{
					for (s = net_freeSockets; s; s = s->next)
					{
						if (q_strcasecmp(Cmd_Argv(1), s->trueaddress) == 0 || q_strcasecmp(Cmd_Argv(1), s->maskedaddress) == 0)
							break;
					}
				}

				if (s == NULL)
					return;

				PrintStats(s);
			}
		}

		// recognize ip:port (based on ProQuake)
		static const char* Strip_Port(const char* host)
		{
			static char noport[MAX_QPATH];
			/* array size as in Host_Connect_f() */
			char* p;
			int			port;

			if (!host || !*host)
				return host;
			q_strlcpy(noport, host, sizeof(noport));
			if ((p = strrchr(noport, ':')) == NULL)
				return host;
			if (strchr(p, ']'))
				return host; //[::] should not be considered port 0
			*p++ = '\0';
			port = atoi(p);
			if (port > 0 && port < 65536 && port != net_hostport)
			{
				net_hostport = port;
				SDL_Log("Port set to %d\n", net_hostport);
			}
			return noport;
		}

		static bool		testInProgress = false;
		static int			testPollCount;
		static int			testDriver;
		static sys_socket_t testSocket;

		PollProcedure testPollProcedure = { NULL, 0.0, Test_Poll };

		static void Test_Poll(void* unused, Engine* engine)
		{
			struct qsockaddr clientaddr;
			int				 control;
			int				 len;
			char			 name[32];
			char			 address[64];
			int				 colors;
			int				 frags;
			int				 connectTime;

			engine->net->landriverlevel = testDriver;

			while (1)
			{
				len = dfunc.Read(testSocket, engine->net->message.data, engine->net->message.maxsize, &clientaddr);
				if (len < (int)sizeof(int))
					break;

				engine->net->message.cursize = len;

				MSG_BeginReading();
				control = BigLong(*((int*)engine->net->message.data));
				MSG_ReadLong();
				if (control == -1)
					break;
				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
					break;
				if ((control & NETFLAG_LENGTH_MASK) != len)
					break;

				if (MSG_ReadByte() != CCREP_PLAYER_INFO)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Unexpected repsonse to Player Info request\n");

				MSG_ReadByte(); /* playerNumber */
				strcpy(name, MSG_ReadString());
				colors = MSG_ReadLong();
				frags = MSG_ReadLong();
				connectTime = MSG_ReadLong();
				strcpy(address, MSG_ReadString());

				SDL_Log("%s\n  frags:%3i  colors:%d %d  time:%d\n  %s\n", name, frags, colors >> 4, colors & 0x0f, connectTime / 60, address);
			}

			testPollCount--;
			if (testPollCount)
			{
				SchedulePollProcedure(&testPollProcedure, 0.1);
			}
			else
			{
				dfunc.Close_Socket(testSocket);
				testInProgress = false;
			}
		}

		void Test_f(void)
		{
			const char* host;
			size_t			 n;
			size_t			 maxusers = MAX_SCOREBOARD;
			struct qsockaddr sendaddr;

			if (testInProgress)
				return;

			host = Strip_Port(Cmd_Argv(1));

			if (host && hostCacheCount)
			{
				for (n = 0; n < hostCacheCount; n++)
				{
					if (q_strcasecmp(host, hostcache[n].name) == 0)
					{
						if (hostcache[n].driver != myDriverLevel)
							continue;
						engine->net->landriverlevel = hostcache[n].ldriver;
						maxusers = hostcache[n].maxusers;
						memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
						break;
					}
				}

				if (n < hostCacheCount)
					goto JustDoIt;
			}

			for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
			{
				if (!net_landrivers[engine->net->landriverlevel].initialized)
					continue;

				// see if we can resolve the host name
				if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
					break;
			}

			if (engine->net->landriverlevel == engine->net->numlandrivers)
			{
				SDL_Log("Could not resolve %s\n", host);
				return;
			}

		JustDoIt:
			testSocket = dfunc.Open_Socket(0);
			if (testSocket == INVALID_SOCKET)
				return;

			testInProgress = true;
			testPollCount = 20;
			testDriver = engine->net->landriverlevel;

			for (n = 0; n < maxusers; n++)
			{
				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				engine->msg->WriteLong(&engine->net->message, 0);
				engine->msg->WriteByte(&engine->net->message, CCREQ_PLAYER_INFO);
				engine->msg->WriteByte(&engine->net->message, n);
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(testSocket, engine->net->message.data, engine->net->message.cursize, &sendaddr);
			}
			engine->sz->Clear(&engine->net->message);
			SchedulePollProcedure(&testPollProcedure, 0.1);
		}

		bool		test2InProgress = false;
		static int			test2Driver;
		static sys_socket_t test2Socket;

		static void			 Test2_Poll(void*);
		PollProcedure test2PollProcedure = { NULL, 0.0, Test2_Poll };

		void Test2_Poll(void* unused)
		{
			struct qsockaddr clientaddr;
			int				 control;
			int				 len;
			char			 name[256];
			char			 value[256];

			engine->net->landriverlevel = test2Driver;
			name[0] = 0;

			len = dfunc.Read(test2Socket, engine->net->message.data, engine->net->message.maxsize, &clientaddr);
			if (len < (int)sizeof(int))
				goto Reschedule;

			engine->net->message.cursize = len;

			MSG_BeginReading();
			control = BigLong(*((int*)engine->net->message.data));
			MSG_ReadLong();
			if (control == -1)
				goto Error;
			if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
				goto Error;
			if ((control & NETFLAG_LENGTH_MASK) != len)
				goto Error;

			if (MSG_ReadByte() != CCREP_RULE_INFO)
				goto Error;

			strcpy(name, MSG_ReadString());
			if (name[0] == 0)
				goto Done;
			strcpy(value, MSG_ReadString());

			SDL_Log("%-16.16s  %-16.16s\n", name, value);

			engine->sz->Clear(&engine->net->message);
			// save space for the header, filled in later
			MSG_WriteLong(&engine->net->message, 0);
			MSG_WriteByte(&engine->net->message, CCREQ_RULE_INFO);
			MSG_WriteString(&engine->net->message, name);
			*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write(test2Socket, engine->net->message.data, engine->net->message.cursize, &clientaddr);
			engine->sz->Clear(&engine->net->message);

		Reschedule:
			SchedulePollProcedure(&test2PollProcedure, 0.05);
			return;

		Error:
			SDL_Log("Unexpected repsonse to Rule Info request\n");
		Done:
			dfunc.Close_Socket(test2Socket);
			test2InProgress = false;
			return;
		}

		void Test2_f(void)
		{
			const char* host;
			size_t			 n;
			struct qsockaddr sendaddr;

			if (test2InProgress)
				return;

			host = Strip_Port(Cmd_Argv(1));

			if (host && hostCacheCount)
			{
				for (n = 0; n < hostCacheCount; n++)
				{
					if (q_strcasecmp(host, hostcache[n].name) == 0)
					{
						if (hostcache[n].driver != myDriverLevel)
							continue;
						engine->net->landriverlevel = hostcache[n].ldriver;
						memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
						break;
					}
				}

				if (n < hostCacheCount)
					goto JustDoIt;
			}

			for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
			{
				if (!net_landrivers[engine->net->landriverlevel].initialized)
					continue;

				// see if we can resolve the host name
				if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
					break;
			}

			if (engine->net->landriverlevel == engine->net->numlandrivers)
			{
				SDL_Log("Could not resolve %s\n", host);
				return;
			}

		JustDoIt:
			test2Socket = dfunc.Open_Socket(0);
			if (test2Socket == INVALID_SOCKET)
				return;

			test2InProgress = true;
			test2Driver = engine->net->landriverlevel;

			engine->sz->Clear(&engine->net->message);
			// save space for the header, filled in later
			MSG_WriteLong(&engine->net->message, 0);
			MSG_WriteByte(&engine->net->message, CCREQ_RULE_INFO);
			MSG_WriteString(&engine->net->message, "");
			*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write(test2Socket, engine->net->message.data, engine->net->message.cursize, &sendaddr);
			engine->sz->Clear(&engine->net->message);
			SchedulePollProcedure(&test2PollProcedure, 0.05);
		}

		int Datagram_Init(void)
		{
			int			 i, num_inited;
			sys_socket_t csock;

#ifdef BAN_TEST
			banAddr.s_addr = INADDR_ANY;
			banMask.s_addr = INADDR_NONE;
#endif
			myDriverLevel = net_driverlevel;

			Cmd_AddCommand("net_stats", NET_Stats_f);

			if (safemode || COM_CheckParm("-nolan"))
				return -1;

			num_inited = 0;
			for (i = 0; i < engine->net->numlandrivers; i++)
			{
				csock = net_landrivers[i].Init();
				if (csock == INVALID_SOCKET)
					continue;
				net_landrivers[i].initialized = true;
				net_landrivers[i].controlSock = csock;
				net_landrivers[i].listeningSock = INVALID_SOCKET;
				num_inited++;
			}

			if (num_inited == 0)
				return -1;

			Cmd_AddCommand("test", Test_f);
			Cmd_AddCommand("test2", Test2_f);

			return 0;
		}

		void Datagram_Shutdown(void)
		{
			int i;

			Datagram_Listen(false);

			//
			// shutdown the lan drivers
			//
			for (i = 0; i < engine->net->numlandrivers; i++)
			{
				if (net_landrivers[i].initialized)
				{
					net_landrivers[i].Shutdown();
					net_landrivers[i].initialized = false;
				}
			}
		}

		void Datagram_Close(qsocket_t* sock)
		{
			if (sock->isvirtual)
			{
				sock->isvirtual = false;
				sock->socket = INVALID_SOCKET;
			}
			else
				sfunc.Close_Socket(sock->socket);
		}

		void Datagram_Listen(bool state)
		{
			qsocket_t* s;
			int		   i;
			bool   islistening = false;

			heartbeat_time = 0; // reset it

			for (i = 0; i < engine->net->numlandrivers; i++)
			{
				if (net_landrivers[i].initialized)
				{
					net_landrivers[i].listeningSock = net_landrivers[i].Listen(state);
					if (net_landrivers[i].listeningSock != INVALID_SOCKET)
						islistening = true;

					for (s = engine->net->engine->net->activeSockets; s; s = s->next)
					{
						if (s->isvirtual)
						{
							s->isvirtual = false;
							s->socket = INVALID_SOCKET;
						}
					}
				}
			}
			if (state && !islistening)
			{
				if (isDedicated)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Unable to open any listening sockets\n");
				Con_Warning("Unable to open any listening sockets\n");
			}
		}

		static struct qsockaddr rcon_response_address;
		static sys_socket_t		rcon_response_socket;
		static sys_socket_t		rcon_response_landriver;
		void					Datagram_Rcon_Flush(const char* text)
		{
			sizebuf_t msg;
			byte	  buffer[8192];
			msg.data = buffer;
			msg.maxsize = sizeof(buffer);
			msg.allowoverflow = true;
			engine->sz->Clear(&msg);
			// save space for the header, filled in later
			MSG_WriteLong(&msg, 0);
			MSG_WriteByte(&msg, CCREP_RCON);
			MSG_WriteString(&msg, text);
			if (msg.overflowed)
				return;
			*((int*)msg.data) = BigLong(NETFLAG_CTL | (msg.cursize & NETFLAG_LENGTH_MASK));
			net_landrivers[rcon_response_landriver].Write(rcon_response_socket, msg.data, msg.cursize, &rcon_response_address);
		}

		void _Datagram_ServerControlPacket(sys_socket_t acceptsock, struct qsockaddr* clientaddr, byte* data, unsigned int length)
		{
			struct qsockaddr newaddr;
			qsocket_t* sock;
			qsocket_t* s;
			int				 command;
			int				 control;
			int				 ret;
			int				 plnum;
			int				 mod; //, mod_ver, mod_flags, mod_passwd;	//proquake extensions

			control = BigLong(*((int*)data));
			if (control == -1)
			{
				if (!sv_public.value)
					return;
				data[length] = 0;
				Cmd_TokenizeString((char*)data + 4);
				if (!strcmp(Cmd_Argv(0), "getinfo") || !strcmp(Cmd_Argv(0), "getstatus"))
				{
					// master, as well as other clients, may send us one of these two packets to get our serverinfo data
					// masters only really need gamename and player counts. actual clients might want player names too.
					bool	 full = !strcmp(Cmd_Argv(0), "getstatus");
					char		 cookie[128];
					const char* str = Cmd_Args();
					const char* gamedir = COM_GetGameNames(false);
					unsigned int numclients = 0, numbots = 0;
					int			 i;
					size_t		 j;
					if (!str)
						str = "";
					q_strlcpy(cookie, str, sizeof(cookie));

					for (i = 0; i < svs.maxclients; i++)
					{
						if (svs.clients[i].active)
						{
							numclients++;
							if (!svs.clients[i].netconnection)
								numbots++;
						}
					}

					engine->sz->Clear(&engine->net->message);
					MSG_WriteLong(&engine->net->message, -1);
					MSG_WriteString(&engine->net->message, full ? "statusResponse\n" : "infoResponse\n");
					engine->net->message.cursize--;
					COM_Parse(com_protocolname.string);
					if (*com_token) // the master server needs this. This tells the master which game we should be listed as.
					{
						MSG_WriteString(&engine->net->message, va("\\gamename\\%s", com_token));
						engine->net->message.cursize--;
					}
					MSG_WriteString(&engine->net->message, "\\protocol\\3");
					engine->net->message.cursize--; // this is stupid
					MSG_WriteString(&engine->net->message, "\\ver\\" ENGINE_NAME_AND_VER);
					engine->net->message.cursize--;
					MSG_WriteString(&engine->net->message, va("\\nqprotocol\\%u", sv.protocol));
					engine->net->message.cursize--;
					if (*gamedir)
					{
						MSG_WriteString(&engine->net->message, va("\\modname\\%s", gamedir));
						engine->net->message.cursize--;
					}
					if (*sv.name)
					{
						MSG_WriteString(&engine->net->message, va("\\mapname\\%s", sv.name));
						engine->net->message.cursize--;
					}
					if (*deathmatch.string)
					{
						MSG_WriteString(&engine->net->message, va("\\deathmatch\\%s", deathmatch.string));
						engine->net->message.cursize--;
					}
					if (*teamplay.string)
					{
						MSG_WriteString(&engine->net->message, va("\\teamplay\\%s", teamplay.string));
						engine->net->message.cursize--;
					}
					if (*hostname.string)
					{
						MSG_WriteString(&engine->net->message, va("\\hostname\\%s", hostname.string));
						engine->net->message.cursize--;
					}
					MSG_WriteString(&engine->net->message, va("\\clients\\%u", numclients));
					engine->net->message.cursize--;
					if (numbots)
					{
						MSG_WriteString(&engine->net->message, va("\\bots\\%u", numbots));
						engine->net->message.cursize--;
					}
					MSG_WriteString(&engine->net->message, va("\\sv_maxclients\\%i", svs.maxclients));
					engine->net->message.cursize--;
					if (*cookie)
					{
						MSG_WriteString(&engine->net->message, va("\\challenge\\%s", cookie));
						engine->net->message.cursize--;
					}

					if (full)
					{
						for (i = 0; i < svs.maxclients; i++)
						{
							if (svs.clients[i].active)
							{
								float total = 0;
								for (j = 0; j < NUM_PING_TIMES; j++)
									total += svs.clients[i].ping_times[j];
								total /= NUM_PING_TIMES;
								total *= 1000; // put it in ms

								MSG_WriteString(
									&engine->net->message, va("\n%i %i %i_%i \"%s\"", svs.clients[i].old_frags, (int)total, svs.clients[i].colors & 15,
										svs.clients[i].colors >> 4, svs.clients[i].name));
								engine->net->message.cursize--;
							}
						}
					}

					dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
					engine->sz->Clear(&engine->net->message);
				}
				return;
			}
			if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
				return;
			if ((control & NETFLAG_LENGTH_MASK) != length)
				return;

			// sigh... FIXME: potentially abusive memcpy
			engine->sz->Clear(&engine->net->message);
			engine->sz->Write(&engine->net->message, data, length);

			MSG_BeginReading();
			MSG_ReadLong();

			command = MSG_ReadByte();
			if (command == CCREQ_SERVER_INFO)
			{
				if (strcmp(MSG_ReadString(), "TREMOR") != 0)
					return;

				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREP_SERVER_INFO);
				dfunc.GetSocketAddr(acceptsock, &newaddr);
				MSG_WriteString(&engine->net->message, dfunc.AddrToString(&newaddr, false));
				MSG_WriteString(&engine->net->message, hostname.string);
				MSG_WriteString(&engine->net->message, sv.name);
				MSG_WriteByte(&engine->net->message, net_activeconnections);
				MSG_WriteByte(&engine->net->message, svs.maxclients);
				MSG_WriteByte(&engine->net->message, NET_PROTOCOL_VERSION);
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
				engine->sz->Clear(&engine->net->message);
				return;
			}

			if (command == CCREQ_PLAYER_INFO)
			{
				int		  playerNumber;
				int		  activeNumber;
				int		  clientNumber;
				client_t* client;

				playerNumber = MSG_ReadByte();
				activeNumber = -1;

				for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
				{
					if (client->active)
					{
						activeNumber++;
						if (activeNumber == playerNumber)
							break;
					}
				}

				if (clientNumber == svs.maxclients)
					return;

				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREP_PLAYER_INFO);
				MSG_WriteByte(&engine->net->message, playerNumber);
				MSG_WriteString(&engine->net->message, client->name);
				MSG_WriteLong(&engine->net->message, client->colors);
				MSG_WriteLong(&engine->net->message, (int)client->edict->v.frags);
				if (!client->netconnection)
				{
					MSG_WriteLong(&engine->net->message, 0);
					MSG_WriteString(&engine->net->message, "Bot");
				}
				else
				{
					MSG_WriteLong(&engine->net->message, (int)(engine->net->time - client->netconnection->connecttime));
					MSG_WriteString(&engine->net->message, NET_QSocketGetMaskedAddressString(client->netconnection));
				}
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
				engine->sz->Clear(&engine->net->message);

				return;
			}

			if (command == CCREQ_RULE_INFO)
			{
				const char* prevCvarName;
				cvar_t* var;

				// find the search start location
				prevCvarName = MSG_ReadString();
				var = Cvar_FindVarAfter(prevCvarName, CVAR_SERVERINFO);

				// send the response
				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREP_RULE_INFO);
				if (var)
				{
					MSG_WriteString(&engine->net->message, var->name);
					MSG_WriteString(&engine->net->message, var->string);
				}
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
				engine->sz->Clear(&engine->net->message);

				return;
			}

			if (command == CCREQ_RCON)
			{
				const char* password = MSG_ReadString(); // FIXME: this really needs crypto
				const char* response;

				rcon_response_address = *clientaddr;
				rcon_response_socket = acceptsock;
				rcon_response_landriver = engine->net->landriverlevel;

				if (!*rcon_password.string)
					response = "rcon is not enabled on this server";
				else if (!strcmp(password, rcon_password.string))
				{
					Con_Redirect(Datagram_Rcon_Flush);
					Cmd_ExecuteString(MSG_ReadString(), src_command);
					Con_Redirect(NULL);
					return;
				}
				else if (!strcmp(password, "password"))
					response = "What, you really thought that would work? Seriously?";
				else if (!strcmp(password, "thebackdoor"))
					response = "Oh look! You found the backdoor. Don't let it slam you in the face on your way out.";
				else
					response = "Your password is just WRONG dude.";

				Datagram_Rcon_Flush(response);
				return;
			}

			if (command != CCREQ_CONNECT)
				return;

			if (strcmp(MSG_ReadString(), "TREMOR") != 0)
				return;

			if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
			{
				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREP_REJECT);
				MSG_WriteString(&engine->net->message, "Incompatible version.\n");
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
				engine->sz->Clear(&engine->net->message);
				return;
			}

			// read proquake extensions
			mod = MSG_ReadByte();
			if (msg_badread)
				mod = 0;
#if 0
			mod_ver = MSG_ReadByte();
			if (msg_badread) mod_ver = 0;
			mod_flags = MSG_ReadByte();
			if (msg_badread) mod_flags = 0;
			mod_passwd = MSG_ReadLong();
			if (msg_badread) mod_passwd = 0;
			(void)mod_ver;
			(void)mod_flags;
			(void)mod_passwd;
#endif

#ifdef BAN_TEST
			// check for a ban
			// fixme: no ipv6
			// fixme: only a single address? someone seriously underestimates tor.
			if (clientaddr->qsa_family == AF_INET)
			{
				in_addr_t testAddr;
				testAddr = ((struct sockaddr_in*)clientaddr)->sin_addr.s_addr;
				if ((testAddr & banMask.s_addr) == banAddr.s_addr)
				{
					engine->sz->Clear(&engine->net->message);
					// save space for the header, filled in later
					MSG_WriteLong(&engine->net->message, 0);
					MSG_WriteByte(&engine->net->message, CCREP_REJECT);
					MSG_WriteString(&engine->net->message, "You have been banned.\n");
					*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
					dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
					engine->sz->Clear(&engine->net->message);
					return;
				}
			}
#endif

			// see if this guy is already connected
			for (s = engine->net->activeSockets; s; s = s->next)
			{
				if (s->driver != net_driverlevel)
					continue;
				if (s->disconnected)
					continue;
				ret = dfunc.AddrCompare(clientaddr, &s->addr);
				if (ret == 0)
				{
					int i;

					// is this a duplicate connection reqeust?
					if (ret == 0 && engine->net->time - s->connecttime < 2.0)
					{
						// yes, so send a duplicate reply
						engine->sz->Clear(&engine->net->message);
						// save space for the header, filled in later
						MSG_WriteLong(&engine->net->message, 0);
						MSG_WriteByte(&engine->net->message, CCREP_ACCEPT);
						dfunc.GetSocketAddr(s->socket, &newaddr);
						MSG_WriteLong(&engine->net->message, dfunc.GetSocketPort(&newaddr));
						if (s->proquake_angle_hack)
						{
							MSG_WriteByte(&engine->net->message, 1);  // proquake
							MSG_WriteByte(&engine->net->message, 30); // ver 30 should be safe. 34 screws with our single-server-socket stuff.
							MSG_WriteByte(&engine->net->message, 0);  // no flags
						}
						*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
						dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
						engine->sz->Clear(&engine->net->message);
						return;
					}
					// it's somebody coming back in from a crash/disconnect
					// so close the old qsocket and let their retry get them back in
					//			NET_Close(s);
					//			return;

					// FIXME: ideally we would just switch the connection over and restart it with a serverinfo packet.
					// warning: there might be packets in-flight which might mess up unreliable sequences.
					// so we attempt to ignore the request, and let the user restart.
					// FIXME: if this is an issue, it should be possible to reuse the previous connection's outgoing unreliable sequence. reliables should be less of an
					// issue as stray ones will be ignored anyway.
					// FIXME: needs challenges, so that other clients can't determine ip's and spoof a reconnect.
					for (i = 0; i < svs.maxclients; i++)
					{
						if (svs.clients[i].netconnection == s)
						{
							NET_Close(s); // close early, to avoid svc_disconnects confusing things.
							host_client = &svs.clients[i];
							SV_DropClient(false);
							break;
						}
					}
					return;
				}
			}

			// find a free player slot
			for (plnum = 0; plnum < svs.maxclients; plnum++)
				if (!svs.clients[plnum].active)
					break;
			if (plnum < svs.maxclients)
				sock = NET_NewQSocket();
			else
				sock = NULL; // can happen due to botclients.

			if (sock == NULL) // no room; try to let him know
			{
				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREP_REJECT);
				MSG_WriteString(&engine->net->message, "Server is full.\n");
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
				engine->sz->Clear(&engine->net->message);
				return;
			}

			sock->proquake_angle_hack = (mod == 1);

			// everything is allocated, just fill in the details
			sock->isvirtual = true;
			sock->socket = acceptsock;
			sock->landriver = engine->net->landriverlevel;
			sock->addr = *clientaddr;
			strcpy(sock->trueaddress, dfunc.AddrToString(clientaddr, false));
			strcpy(sock->maskedaddress, dfunc.AddrToString(clientaddr, true));

			// send him back the info about the server connection he has been allocated
			engine->sz->Clear(&engine->net->message);
			// save space for the header, filled in later
			MSG_WriteLong(&engine->net->message, 0);
			MSG_WriteByte(&engine->net->message, CCREP_ACCEPT);
			dfunc.GetSocketAddr(sock->socket, &newaddr);
			MSG_WriteLong(&engine->net->message, dfunc.GetSocketPort(&newaddr));
			//	MSG_WriteString(&engine->net->message, dfunc.AddrToString(&newaddr));
			if (sock->proquake_angle_hack)
			{
				MSG_WriteByte(&engine->net->message, 1);  // proquake
				MSG_WriteByte(&engine->net->message, 30); // ver 30 should be safe. 34 screws with our single-server-socket stuff.
				MSG_WriteByte(&engine->net->message, 0);
			}
			*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write(acceptsock, engine->net->message.data, engine->net->message.cursize, clientaddr);
			engine->sz->Clear(&engine->net->message);

			// spawn the client.
			// FIXME: come up with some challenge mechanism so that we don't go to the expense of spamming serverinfos+modellists+etc until we know that its an actual
			// connection attempt.
			svs.clients[plnum].netconnection = sock;
			SV_ConnectClient(plnum);
		}

		qsocket_t* Datagram_CheckNewConnections(void)
		{
			// only needs to do master stuff now
			if (sv_public.value > 0)
			{
				if (Sys_DoubleTime() > heartbeat_time)
				{
					// darkplaces here refers to the master server protocol, rather than the game protocol
					//(specifies that the server responds to infoRequest packets from the master)
					char			 str[] = "\377\377\377\377heartbeat DarkPlaces\n";
					size_t			 k;
					struct qsockaddr addr;
					heartbeat_time = Sys_DoubleTime() + 300;

					for (k = 0; net_masters[k].string; k++)
					{
						if (!*net_masters[k].string)
							continue;
						for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
						{
							if (net_landrivers[engine->net->landriverlevel].initialized && dfunc.listeningSock != INVALID_SOCKET)
							{
								if (dfunc.GetAddrFromName(net_masters[k].string, &addr) >= 0)
								{
									if (sv_reportheartbeats.value)
										SDL_Log("Sending heartbeat to %s\n", net_masters[k].string);
									dfunc.Write(dfunc.listeningSock, (byte*)str, strlen(str), &addr);
								}
								else
								{
									if (sv_reportheartbeats.value)
										SDL_Log("Unable to resolve %s\n", net_masters[k].string);
								}
							}
						}
					}
				}
			}

			return NULL;
		}

		void _Datagram_SendServerQuery(struct qsockaddr* addr, bool master)
		{
			engine->sz->Clear(&engine->net->message);
			if (master) // assume false if you want only the protocol 15 servers.
			{
				MSG_WriteLong(&engine->net->message, ~0);
				MSG_WriteString(&engine->net->message, "getinfo");
			}
			else
			{
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREQ_SERVER_INFO);
				MSG_WriteString(&engine->net->message, "TREMOR");
				MSG_WriteByte(&engine->net->message, NET_PROTOCOL_VERSION);
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
			}
			dfunc.Write(dfunc.controlSock, engine->net->message.data, engine->net->message.cursize, addr);
			engine->sz->Clear(&engine->net->message);
		}
		static struct
		{
			int				 driver;
			bool		 requery;
			bool		 master;
			struct qsockaddr addr;
		}		   *hostlist;
		size_t		hostlist_count;
		size_t		hostlist_max;
		void _Datagram_AddPossibleHost(struct qsockaddr* addr, bool master)
		{
			size_t u;
			for (u = 0; u < hostlist_count; u++)
			{
				if (!memcmp(&hostlist[u].addr, addr, sizeof(struct qsockaddr)) && hostlist[u].driver == engine->net->landriverlevel)
				{ // we already know about it. it must have come from some other master. don't respam.
					return;
				}
			}
			if (hostlist_count == hostlist_max)
			{
				hostlist_max = hostlist_count + 16;
				hostlist = Mem_Realloc(hostlist, sizeof(*hostlist) * hostlist_max);
			}
			hostlist[hostlist_count].addr = *addr;
			hostlist[hostlist_count].requery = true;
			hostlist[hostlist_count].master = master;
			hostlist[hostlist_count].driver = engine->net->landriverlevel;
			hostlist_count++;
		}

		void Info_ReadKey(const char* info, const char* key, char* out, size_t outsize)
		{
			size_t keylen = strlen(key);
			while (*info)
			{
				if (*info++ != '\\')
					break; // error / end-of-string

				if (!strncmp(info, key, keylen) && info[keylen] == '\\')
				{
					char* o = out, * e = out + outsize - 1;

					// skip the key name
					info += keylen + 1;
					// this is the old value for the key. copy it to the result
					while (*info && *info != '\\' && o < e)
						*o++ = *info++;
					*o++ = 0;

					// success!
					return;
				}
				else
				{
					// skip the key
					while (*info && *info != '\\')
						info++;

					// validate that its a value now
					if (*info++ != '\\')
						break; // error
					// skip the value
					while (*info && *info != '\\')
						info++;
				}
			}
			*out = 0;
		}

		static bool _Datagram_SearchForHosts(bool xmit, Engine* engine)
		{
			int				 ret;
			size_t			 n;
			size_t			 i;
			struct qsockaddr readaddr;
			struct qsockaddr myaddr;
			int				 control;
			bool		 sentsomething = false;

			dfunc.GetSocketAddr(dfunc.controlSock, &myaddr);
			if (xmit)
			{
				for (i = 0; i < hostlist_count; i++)
					hostlist[i].requery = true;

				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREQ_SERVER_INFO);
				MSG_WriteString(&engine->net->message, "TREMOR");
				MSG_WriteByte(&engine->net->message, NET_PROTOCOL_VERSION);
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Broadcast(dfunc.controlSock, engine->net->message.data, engine->net->message.cursize);
				engine->sz->Clear(&engine->net->message);

				if (slist_scope == SLIST_INTERNET)
				{
					struct qsockaddr masteraddr;
					char* str;
					size_t			 m;
					for (m = 0; net_masters[m].string; m++)
					{
						if (!*net_masters[m].string)
							continue;
						if (dfunc.GetAddrFromName(net_masters[m].string, &masteraddr) >= 0)
						{
							const char* prot = com_protocolname.string;
							while (*prot)
							{ // send a request for each protocol
								prot = COM_Parse(prot);
								if (!prot)
									break;
								if (*com_token)
								{
									if (masteraddr.qsa_family == AF_INET6)
										str = va("%c%c%c%cgetserversExt %s %u empty full ipv6" /*\x0A\n"*/, 255, 255, 255, 255, com_token, NET_PROTOCOL_VERSION);
									else
										str = va("%c%c%c%cgetservers %s %u empty full" /*\x0A\n"*/, 255, 255, 255, 255, com_token, NET_PROTOCOL_VERSION);
									dfunc.Write(dfunc.controlSock, (byte*)str, strlen(str), &masteraddr);
								}
							}
						}
					}
				}
				sentsomething = true;
			}

			while ((ret = dfunc.Read(dfunc.controlSock, engine->net->message.data, engine->net->message.maxsize, &readaddr)) > 0)
			{
				if (ret < (int)sizeof(int))
					continue;
				engine->net->message.cursize = ret;

				// don't answer our own query
				// Note: this doesn't really work too well if we're multi-homed.
				// we should probably just refuse to respond to serverinfo requests while we're scanning (chances are our server is going to die anyway).
				if (dfunc.AddrCompare(&readaddr, &myaddr) >= 0)
					continue;

				// is the cache full?
				if (hostCacheCount == HOSTCACHESIZE)
					continue;

				MSG_BeginReading();
				control = BigLong(*((int*)engine->net->message.data));
				MSG_ReadLong();
				if (control == -1)
				{
					if (msg_readcount + 19 <= engine->net->message.cursize && !strncmp((char*)engine->net->message.data + msg_readcount, "getserversResponse", 18))
					{
						struct qsockaddr addr;
						int				 j;
						msg_readcount += 18;
						for (;;)
						{
							switch (MSG_ReadByte())
							{
							case '\\':
								memset(&addr, 0, sizeof(addr));
								addr.qsa_family = AF_INET;
								for (j = 0; j < 4; j++)
									((byte*)&((struct sockaddr_in*)&addr)->sin_addr)[j] = MSG_ReadByte();
								((byte*)&((struct sockaddr_in*)&addr)->sin_port)[0] = MSG_ReadByte();
								((byte*)&((struct sockaddr_in*)&addr)->sin_port)[1] = MSG_ReadByte();
								if (!((struct sockaddr_in*)&addr)->sin_port)
									msg_badread = true;
								break;
							case '/':
								memset(&addr, 0, sizeof(addr));
								addr.qsa_family = AF_INET6;
								for (j = 0; j < 16; j++)
									((byte*)&((struct sockaddr_in6*)&addr)->sin6_addr)[j] = MSG_ReadByte();
								((byte*)&((struct sockaddr_in6*)&addr)->sin6_port)[0] = MSG_ReadByte();
								((byte*)&((struct sockaddr_in6*)&addr)->sin6_port)[1] = MSG_ReadByte();
								if (!((struct sockaddr_in6*)&addr)->sin6_port)
									msg_badread = true;
								break;
							default:
								memset(&addr, 0, sizeof(addr));
								msg_badread = true;
								break;
							}
							if (msg_badread)
								break;
							_Datagram_AddPossibleHost(&addr, true);
							sentsomething = true;
						}
					}
					else if (msg_readcount + 13 <= engine->net->message.cursize && !strncmp((char*)engine->net->message.data + msg_readcount, "infoResponse\n", 13))
					{ // response from a dpp7 server (or possibly 15, no idea really)
						char		tmp[1024];
						const char* info = MSG_ReadString() + 13;

						// search the cache for this server
						for (n = 0; n < hostCacheCount; n++)
						{
							if (dfunc.AddrCompare(&readaddr, &hostcache[n].addr) == 0)
								break;
						}

						// is it already there?
						if (n < hostCacheCount)
						{
							if (*hostcache[n].cname)
								continue;
						}
						else
						{
							// add it
							hostCacheCount++;
						}
						Info_ReadKey(info, "hostname", hostcache[n].name, sizeof(hostcache[n].name));
						if (!*hostcache[n].name)
							q_strlcpy(hostcache[n].name, "UNNAMED", sizeof(hostcache[n].name));
						Info_ReadKey(info, "mapname", hostcache[n].map, sizeof(hostcache[n].map));
						Info_ReadKey(info, "modname", hostcache[n].gamedir, sizeof(hostcache[n].gamedir));

						Info_ReadKey(info, "clients", tmp, sizeof(tmp));
						hostcache[n].users = atoi(tmp);
						Info_ReadKey(info, "sv_maxclients", tmp, sizeof(tmp));
						hostcache[n].maxusers = atoi(tmp);
						Info_ReadKey(info, "protocol", tmp, sizeof(tmp));
						if (atoi(tmp) != NET_PROTOCOL_VERSION)
						{
							strcpy(hostcache[n].cname, hostcache[n].name);
							strcpy(hostcache[n].name, "*");
							strcat(hostcache[n].name, hostcache[n].cname);
						}
						memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
						hostcache[n].driver = net_driverlevel;
						hostcache[n].ldriver = engine->net->landriverlevel;
						q_strlcpy(hostcache[n].cname, dfunc.AddrToString(&readaddr, false), sizeof(hostcache[n].cname));

						// check for a name conflict
						for (i = 0; i < hostCacheCount; i++)
						{
							if (i == n)
								continue;
							if (q_strcasecmp(hostcache[n].cname, hostcache[i].cname) == 0)
							{ // this is a dupe.
								hostCacheCount--;
								break;
							}
							if (q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0)
							{
								i = strlen(hostcache[n].name);
								if (i < 15 && hostcache[n].name[i - 1] > '8')
								{
									hostcache[n].name[i] = '0';
									hostcache[n].name[i + 1] = 0;
								}
								else
									hostcache[n].name[i - 1]++;

								i = (size_t)-1;
							}
						}
					}
					continue;
				}
				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
					continue;
				if ((control & NETFLAG_LENGTH_MASK) != ret)
					continue;

				if (MSG_ReadByte() != CCREP_SERVER_INFO)
					continue;

				MSG_ReadString();
				// dfunc.GetAddrFromName(MSG_ReadString(), &peeraddr);
				/*if (dfunc.AddrCompare(&readaddr, &peeraddr) != 0)
				{
					char read[NET_NAMELEN];
					char peer[NET_NAMELEN];
					q_strlcpy(read, dfunc.AddrToString(&readaddr), sizeof(read));
					q_strlcpy(peer, dfunc.AddrToString(&peeraddr), sizeof(peer));
					SDL_Log("Server at %s claimed to be at %s\n", read, peer);
				}*/

				// search the cache for this server
				for (n = 0; n < hostCacheCount; n++)
				{
					if (dfunc.AddrCompare(&readaddr, &hostcache[n].addr) == 0)
						break;
				}

				// is it already there?
				if (n < hostCacheCount)
				{
					if (*hostcache[n].cname)
						continue;
				}
				else
				{
					// add it
					hostCacheCount++;
				}
				q_strlcpy(hostcache[n].name, MSG_ReadString(), sizeof(hostcache[n].name));
				if (!*hostcache[n].name)
					q_strlcpy(hostcache[n].name, "UNNAMED", sizeof(hostcache[n].name));
				q_strlcpy(hostcache[n].map, MSG_ReadString(), sizeof(hostcache[n].map));
				hostcache[n].users = MSG_ReadByte();
				hostcache[n].maxusers = MSG_ReadByte();
				if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
				{
					strcpy(hostcache[n].cname, hostcache[n].name);
					hostcache[n].cname[14] = 0;
					strcpy(hostcache[n].name, "*");
					strcat(hostcache[n].name, hostcache[n].cname);
				}
				memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
				hostcache[n].driver = net_driverlevel;
				hostcache[n].ldriver = engine->net->landriverlevel;
				q_strlcpy(hostcache[n].cname, dfunc.AddrToString(&readaddr, false), sizeof(hostcache[n].cname));

				// check for a name conflict
				for (i = 0; i < hostCacheCount; i++)
				{
					if (i == n)
						continue;
					if (q_strcasecmp(hostcache[n].cname, hostcache[i].cname) == 0)
					{ // this is a dupe.
						hostCacheCount--;
						break;
					}
					if (q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0)
					{
						i = strlen(hostcache[n].name);
						if (i < 15 && hostcache[n].name[i - 1] > '8')
						{
							hostcache[n].name[i] = '0';
							hostcache[n].name[i + 1] = 0;
						}
						else
							hostcache[n].name[i - 1]++;

						i = (size_t)-1;
					}
				}
			}

			if (!xmit)
			{
				n = 4; // should be time-based. meh.
				for (i = 0; i < hostlist_count; i++)
				{
					if (hostlist[i].requery && hostlist[i].driver == engine->net->landriverlevel)
					{
						hostlist[i].requery = false;
						_Datagram_SendServerQuery(&hostlist[i].addr, hostlist[i].master);
						sentsomething = true;
						n--;
						if (!n)
							break;
					}
				}
			}
			return sentsomething;
		}

		bool Datagram_SearchForHosts(bool xmit)
		{
			bool ret = false;
			for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
			{
				if (hostCacheCount == HOSTCACHESIZE)
					break;
				if (net_landrivers[engine->net->landriverlevel].initialized)
					ret |= _Datagram_SearchForHosts(xmit, engine);
			}
			return ret;
		}

		static qsocket_t* _Datagram_Connect(struct qsockaddr* serveraddr, Engine* engine)
		{
			struct qsockaddr readaddr;
			qsocket_t* sock;
			sys_socket_t	 newsock;
			int				 ret;
			int				 reps;
			double			 start_time;
			int				 control;
			const char* reason;

			newsock = dfunc.Open_Socket(0);
			if (newsock == INVALID_SOCKET)
				return NULL;

			sock = engine->net->NewQSocket();
			if (sock == NULL)
				goto ErrorReturn2;
			sock->socket = newsock;
			sock->landriver = engine->net->landriverlevel;

			// connect to the host
			if (dfunc.Connect(newsock, serveraddr) == -1)
				goto ErrorReturn;

			sock->proquake_angle_hack = true;

			// send the connection request
			SDL_Log("trying...\n");
			//SCR_UpdateScreen(false);
			start_time = engine->net->time;

			for (reps = 0; reps < 3; reps++)
			{
				engine->sz->Clear(&engine->net->message);
				// save space for the header, filled in later
				MSG_WriteLong(&engine->net->message, 0);
				MSG_WriteByte(&engine->net->message, CCREQ_CONNECT);
				MSG_WriteString(&engine->net->message, "TREMOR");
				MSG_WriteByte(&engine->net->message, NET_PROTOCOL_VERSION);
				if (sock->proquake_angle_hack)
				{ /*Spike -- proquake compat. if both engines claim to be using mod==1 then 16bit client->server angles can be used. server->client angles remain
					 16bit*/
					Con_DWarning("Attempting to use ProQuake angle hack\n");
					MSG_WriteByte(&engine->net->message, 1);  /*'mod', 1=proquake*/
					MSG_WriteByte(&engine->net->message, 34); /*'mod' version*/
					MSG_WriteByte(&engine->net->message, 0);  /*flags*/
					MSG_WriteLong(&engine->net->message, 0);  // strtoul(password.string, NULL, 0)); /*password*/
				}
				*((int*)engine->net->message.data) = BigLong(NETFLAG_CTL | (engine->net->message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write(newsock, engine->net->message.data, engine->net->message.cursize, serveraddr);
				engine->sz->Clear(&engine->net->message);

				// for dp compat. DP sends these in addition to the above packet.
				// if the (DP) server is running using vanilla protocols, it replies to the above, otherwise to the following, requiring both to be sent.
				//(challenges hinder a DOS issue known as smurfing, in that the client must prove that it owns the IP that it might be spoofing before any serious resources are
				// used)
#define DPGETCHALLENGE "\xff\xff\xff\xffgetchallenge\n"
				dfunc.Write(newsock, (byte*)DPGETCHALLENGE, strlen(DPGETCHALLENGE), serveraddr);

				do
				{
					ret = dfunc.Read(newsock, engine->net->message.data, engine->net->message.maxsize, &readaddr);
					// if we got something, validate it
					if (ret > 0)
					{
						// is it from the right place?
						if (dfunc.AddrCompare(&readaddr, serveraddr) != 0)
						{
							SDL_Log("wrong reply address\n");
							SDL_Log("Expected: %s | %s\n", dfunc.AddrToString(serveraddr, false), StrAddr(serveraddr));
							SDL_Log("Received: %s | %s\n", dfunc.AddrToString(&readaddr, false), StrAddr(&readaddr));
							SCR_UpdateScreen(false);
							ret = 0;
							continue;
						}

						if (ret < (int)sizeof(int))
						{
							ret = 0;
							continue;
						}

						engine->net->message.cursize = ret;
						MSG_BeginReading();

						control = BigLong(*((int*)engine->net->message.data));
						MSG_ReadLong();
						if (control == -1)
						{
							const char* s = MSG_ReadString();
							if (!strncmp(s, "challenge ", 10))
							{ // either a q2 or dp server...
								char buf[1024];
								q_snprintf(
									buf, sizeof(buf), "%c%c%c%cconnect\\protocol\\darkplaces 3\\protocols\\RMQ FITZ DP7 NEHAHRABJP3 QUAKE\\challenge\\%s", 255, 255,
									255, 255, s + 10);
								dfunc.Write(newsock, (byte*)buf, strlen(buf), serveraddr);
							}
							else if (!strcmp(s, "accept"))
							{
								memcpy(&sock->addr, serveraddr, sizeof(struct qsockaddr));
								sock->proquake_angle_hack = false;
								goto dpserveraccepted;
							}
							/*else if (!strcmp(s, "reject"))
							{
								reason = MSG_ReadString();
								SDL_Log("%s\n", reason);
								q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
								goto ErrorReturn;
							}*/

							ret = 0;
							continue;
						}
						if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
						{
							ret = 0;
							continue;
						}
						if ((control & NETFLAG_LENGTH_MASK) != ret)
						{
							ret = 0;
							continue;
						}
					}
				} while (ret == 0 && (SetNetTime() - start_time) < 2.5);

				if (ret)
					break;

				SDL_Log("still trying...\n");
				SCR_UpdateScreen(false);
				start_time = SetNetTime();
			}

			if (ret == 0)
			{
				reason = "No Response";
				SDL_Log("%s\n", reason);
				strcpy(m_return_reason, reason);
				goto ErrorReturn;
			}

			if (ret == -1)
			{
				reason = "Network Error";
				SDL_Log("%s\n", reason);
				strcpy(m_return_reason, reason);
				goto ErrorReturn;
			}

			ret = MSG_ReadByte();
			if (ret == CCREP_REJECT)
			{
				reason = MSG_ReadString();
				SDL_Log("%s\n", reason);
				q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
				goto ErrorReturn;
			}

			if (ret == CCREP_ACCEPT)
			{
				int port;
				memcpy(&sock->addr, serveraddr, sizeof(struct qsockaddr));
				port = MSG_ReadLong();
				if (port) // spike --- don't change the remote port if the server doesn't want us to. this allows servers to use port forwarding with less issues,
					// assuming the server uses the same port for all clients.
					dfunc.SetSocketPort(&sock->addr, port);
			}
			else
			{
				reason = "Bad Response";
				SDL_Log("%s\n", reason);
				strcpy(m_return_reason, reason);
				goto ErrorReturn;
			}

			if (sock->proquake_angle_hack)
			{
				byte mod = (msg_readcount < engine->net->message.cursize) ? MSG_ReadByte() : 0;
				byte ver = (msg_readcount < engine->net->message.cursize) ? MSG_ReadByte() : 0;
				byte flags = (msg_readcount < engine->net->message.cursize) ? MSG_ReadByte() : 0;
				(void)ver;

				if (mod == 1 /*MOD_PROQUAKE*/)
				{
					if (flags & 1 /*CHEATFREE*/)
					{
						reason = "Server is incompatible";
						SDL_Log("%s\n", reason);
						strcpy(m_return_reason, reason);
						goto ErrorReturn;
					}
					sock->proquake_angle_hack = true;
				}
				else
					sock->proquake_angle_hack = false;
			}

		dpserveraccepted:

			dfunc.GetNameFromAddr(serveraddr, sock->trueaddress);
			dfunc.GetNameFromAddr(serveraddr, sock->maskedaddress);

			SDL_Log("Connection accepted\n");
			sock->lastMessageTime = SetNetTime();

			// switch the connection to the specified address
			if (dfunc.Connect(newsock, &sock->addr) == -1)
			{
				reason = "Connect to Game failed";
				SDL_Log("%s\n", reason);
				strcpy(m_return_reason, reason);
				goto ErrorReturn;
			}

			/*Spike's rant about NATs:
			We sent a packet to the server's control port.
			The server replied from that control port. all is well so far.
			The server is now about(or already did, yay resends) to send us a packet from its data port to our address.
			The nat will (correctly) see a packet from a different remote address:port.
			The local nat has two options here. 1) assume that the wrong port is fine. 2) drop it. Dropping it is far more likely.
			The NQ code will not send any unreliables until we have received the serverinfo. There are no reliables that need to be sent either.
			Normally we won't send ANYTHING until we get that packet.
			Which will never happen because the NAT will never let it through.
			So, if we want to get away without fixing everyone else's server (which is also quite messy),
				the easy way around this dilema is to just send some (small) useless packet to what we believe to be the server's data port.
			A single unreliable clc_nop should do it. There's unlikely to be much packetloss on our local lan (so long as our host buffers outgoing packets on a
			per-socket basis or something), so we don't normally need to resend. We don't really care if the server can even read it properly, but its best to avoid
			warning prints. With that small outgoing packet, our local nat will think we initiated the request. HOPEFULLY it'll reuse the same public port+address. Most
			home routers will, but not all, most hole-punching techniques depend upon such behaviour. Note that proquake 3.4+ will actually wait for a packet from the
			new client, which solves that (but makes the nop mandatory, so needs to be reliable).

			the nop is actually sent inside CL_EstablishConnection where it has cleaner access to the client's pending reliable message.

			Note that we do fix our own server. This means that we can easily run on a home nat. the heartbeats to the master will open up a public port with most
			routers. And if that doesn't work, then its easy enough to port-forward a single known port instead of having to DMZ the entire network. I don't really
			expect that many people will use this, but it'll be nice for the occasional coop game. (note that this makes the nop redundant, but that's a different can
			of worms)
			*/

			m_return_onerror = false;
			return sock;

		ErrorReturn:
			NET_FreeQSocket(sock);
		ErrorReturn2:
			dfunc.Close_Socket(newsock);
			if (m_return_onerror)
			{
				key_dest = key_menu;
				m_state = m_return_state;
				m_return_onerror = false;
			}
			return NULL;
		}

		qsocket_t* Datagram_Connect(const char* host)
		{
			qsocket_t* ret = NULL;
			bool		 resolved = false;
			struct qsockaddr addr;

			host = Strip_Port(host);
			for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
			{
				if (net_landrivers[engine->net->landriverlevel].initialized)
				{
					// see if we can resolve the host name
					// Spike -- moved name resolution to here to avoid extraneous 'could not resolves' when using other address families
					if (dfunc.GetAddrFromName(host, &addr) != -1)
					{
						resolved = true;
						if ((ret = _Datagram_Connect(&addr)) != NULL)
							break;
					}
				}
			}
			if (!resolved)
				SDL_Log("Could not resolve %s\n", host);
			return ret;
		}

		/*
		Spike: added this to list more than one ipv4 address (many people are still multi-homed)
		*/
		int Datagram_QueryAddresses(qhostaddr_t* addresses, int maxaddresses)
		{
			int result = 0;
			for (engine->net->landriverlevel = 0; engine->net->landriverlevel < engine->net->numlandrivers; engine->net->landriverlevel++)
			{
				if (!engine->net->landrivers[engine->net->landriverlevel].initialized)
					continue;
				if (result == maxaddresses)
					break;
				if (engine->net->landrivers[engine->net->landriverlevel].QueryAddresses)
					result += engine->net->landrivers[engine->net->landriverlevel].QueryAddresses(addresses + result, maxaddresses - result);
			}
			return result;
		}

	};

	class WINIP {
	public:

		// ipv4 defs
		sys_socket_t		  netv4_acceptsocket = INVALID_SOCKET; // socket for fielding new connections
		sys_socket_t		  netv4_controlsocket;
		sys_socket_t		  netv4_broadcastsocket = INVALID_SOCKET;
		struct sockaddr_in broadcastaddrv4;
		in_addr_t		  myAddrv4, bindAddrv4; // spike --keeping separate bind and detected values.

		// ipv6 defs
#ifdef IPPROTO_IPV6
		typedef struct in_addr6	   in_addr6_t;
		sys_socket_t		   netv6_acceptsocket = INVALID_SOCKET; // socket for fielding new connections
		sys_socket_t		   netv6_controlsocket;
		struct sockaddr_in6 broadcastaddrv6;
		in_addr6_t		   myAddrv6, bindAddrv6;
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#endif

		Engine* engine;
		WINIP(Engine* e){
			engine = e;
		}
		void WINIPv4_GetLocalAddress(void)
		{
			struct hostent* local = NULL;
			char			buff[MAXHOSTNAMELEN];
			in_addr_t		addr;
			int				err;

			if (myAddrv4 != INADDR_ANY)
				return;

			if (gethostname(buff, MAXHOSTNAMELEN) == SOCKET_ERROR)
			{
				err = SOCKETERRNO;
				SDL_Log("WINIPV4_GetLocalAddress: gethostname failed (%s)\n", socketerror(err));
				return;
			}

			buff[MAXHOSTNAMELEN - 1] = 0;
			local = gethostbyname(buff);
			err = WSAGetLastError();

			if (local == NULL)
			{
				SDL_Log("WINIPV4_GetLocalAddress: gethostbyname failed (%s)\n", __WSAE_StrError(err));
				return;
			}

			myAddrv4 = *(in_addr_t*)local->h_addr_list[0];

			addr = ntohl(myAddrv4);
			_snprintf_s(engine->net->my_ipv4_address, NET_NAMELEN, NET_NAMELEN, "%ld.%ld.%ld.%ld", (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);
		}

		sys_socket_t WINIPv4_Init(void)
		{
			int	 i, err;
			char buff[MAXHOSTNAMELEN];

			if (engine->com->CheckParm("-noudp") || engine->com->CheckParm("-noudp4"))
				return (sys_socket_t)-1;

			if (winsock_initialized == 0)
			{
				err = WSAStartup(MAKEWORD(1, 1), &winsockdata);
				if (err != 0)
				{
					SDL_Log("Winsock initialization failed (%s)\n", socketerror(err));
					return INVALID_SOCKET;
				}
			}
			winsock_initialized++;

			// determine my name & address
			if (gethostname(buff, MAXHOSTNAMELEN) != 0)
			{
				err = SOCKETERRNO;
				SDL_Log("WINS_Init: gethostname failed (%s)\n", socketerror(err));
			}
			else
			{
				buff[MAXHOSTNAMELEN - 1] = 0;
			}
			i = engine->com->CheckParm("-ip");
			if (i)
			{
				if (i < engine->argc - 1)
				{
					bindAddrv4 = inet_addr(engine->argv[i + 1]);
					if (bindAddrv4 == INADDR_NONE)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"%s is not a valid IP address", engine->argv[i + 1]);
					strcpy(engine->net->my_ipv4_address, engine->argv[i + 1]);
				}
				else
				{
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"NET_Init: you must specify an IP address after -ip");
				}
			}
			else
			{
				bindAddrv4 = INADDR_ANY;
				strcpy(engine->net->my_ipv4_address, "INADDR_ANY");
			}

			myAddrv4 = bindAddrv4;

			if ((netv4_controlsocket = WINIPv4_OpenSocket(0)) == INVALID_SOCKET)
			{
				SDL_Log("WINS_Init: Unable to open control socket, UDP disabled\n");
				if (--winsock_initialized == 0)
					WSACleanup();
				return INVALID_SOCKET;
			}

			broadcastaddrv4.sin_family = AF_INET;
			broadcastaddrv4.sin_addr.s_addr = INADDR_BROADCAST;
			broadcastaddrv4.sin_port = htons((unsigned short)net_hostport);

			SDL_Log("IPv4 UDP Initialized\n");
			ipv4Available = true;

			return netv4_controlsocket;
		}

		//=============================================================================

		void WINIPv4_Shutdown(void)
		{
			WINIPv4_Listen(false);
			WINS_CloseSocket(netv4_controlsocket);
			if (--winsock_initialized == 0)
				WSACleanup();
		}

		//=============================================================================

		sys_socket_t WINIPv4_Listen(bool state)
		{
			// enable listening
			if (state)
			{
				if (netv4_acceptsocket != INVALID_SOCKET)
					return netv4_acceptsocket;
				WINIPv4_GetLocalAddress();
				netv4_acceptsocket = WINIPv4_OpenSocket(net_hostport);
				return netv4_acceptsocket;
			}

			// disable listening
			if (netv4_acceptsocket == INVALID_SOCKET)
				return INVALID_SOCKET;
			WINS_CloseSocket(netv4_acceptsocket);
			netv4_acceptsocket = INVALID_SOCKET;
			return INVALID_SOCKET;
		}

		//=============================================================================

		sys_socket_t WINIPv4_OpenSocket(int port)
		{
			sys_socket_t	   newsocket;
			struct sockaddr_in address;
			u_long			   _true = 1;
			int				   err;

			if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
			{
				err = SOCKETERRNO;
				SDL_Log("WINS_OpenSocket: %s\n", socketerror(err));
				return INVALID_SOCKET;
			}

			if (ioctlsocket(newsocket, FIONBIO, &_true) == SOCKET_ERROR)
				goto ErrorReturn;

			memset(&address, 0, sizeof(struct sockaddr_in));
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = bindAddrv4;
			address.sin_port = htons((unsigned short)port);
			if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == 0)
				return newsocket;

			if (ipv4Available)
			{
				err = SOCKETERRNO;
				SDL_Log("Unable to bind to %s (%s)\n", WINS_AddrToString((struct qsockaddr*)&address, false), socketerror(err));
				return INVALID_SOCKET; /* not reached */
			}
			/* else: we are still in init phase, no need to error */

		ErrorReturn:
			err = SOCKETERRNO;
			SDL_Log("WINS_OpenSocket: %s\n", socketerror(err));
			closesocket(newsocket);
			return INVALID_SOCKET;
		}

		//=============================================================================

		int WINS_CloseSocket(sys_socket_t socketid)
		{
			if (socketid == netv4_broadcastsocket)
				netv4_broadcastsocket = INVALID_SOCKET;
			return closesocket(socketid);
		}

		//=============================================================================

		/*
		============
		PartialIPAddress

		this lets you type only as much of the net address as required, using
		the local network components to fill in the rest
		============
		*/
		int PartialIPAddress(const char* in, struct qsockaddr* hostaddr)
		{
			char  buff[256];
			char* b;
			int	  addr, mask, num, port, run;

			buff[0] = '.';
			b = buff;
			strcpy(buff + 1, in);
			if (buff[1] == '.')
				b++;

			addr = 0;
			mask = -1;
			while (*b == '.')
			{
				b++;
				num = 0;
				run = 0;
				while (!(*b < '0' || *b > '9'))
				{
					num = num * 10 + *b++ - '0';
					if (++run > 3)
						return -1;
				}
				if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
					return -1;
				if (num < 0 || num > 255)
					return -1;
				mask <<= 8;
				addr = (addr << 8) + num;
			}

			if (*b++ == ':')
				port = atoi(b);
			else
				port = net_hostport;

			hostaddr->qsa_family = AF_INET;
			((struct sockaddr_in*)hostaddr)->sin_port = htons((unsigned short)port);
			((struct sockaddr_in*)hostaddr)->sin_addr.s_addr = (myAddrv4 & htonl(mask)) | htonl(addr);

			return 0;
		}

		//=============================================================================

		int WINS_Connect(sys_socket_t socketid, struct qsockaddr* addr)
		{
			return 0;
		}

		//=============================================================================

		sys_socket_t WINIPv4_CheckNewConnections(void)
		{
			char buf[4096];

			if (netv4_acceptsocket == INVALID_SOCKET)
				return INVALID_SOCKET;

			if (recvfrom(netv4_acceptsocket, buf, sizeof(buf), MSG_PEEK, NULL, NULL) != SOCKET_ERROR)
			{
				return netv4_acceptsocket;
			}
			return INVALID_SOCKET;
		}

		//=============================================================================

		int WINS_Read(sys_socket_t socketid, byte* buf, int len, struct qsockaddr* addr)
		{
			socklen_t addrlen = sizeof(struct qsockaddr);
			int		  ret;

			ret = recvfrom(socketid, (char*)buf, len, 0, (struct sockaddr*)addr, &addrlen);
			if (ret == SOCKET_ERROR)
			{
				int err = SOCKETERRNO;
				if (err == NET_EWOULDBLOCK || err == NET_ECONNREFUSED)
					return 0;
				if (err == WSAECONNRESET)
					SDL_Log("WINS_Read, recvfrom: %s (%s)\n", socketerror(err), WINS_AddrToString(addr, false));
				else
					SDL_Log("WINS_Read, recvfrom: %s\n", socketerror(err));
			}
			return ret;
		}

		//=============================================================================

		static int WINS_MakeSocketBroadcastCapable(sys_socket_t socketid)
		{
			int i = 1;

			// make this socket broadcast capable
			if (setsockopt(socketid, SOL_SOCKET, SO_BROADCAST, (char*)&i, sizeof(i)) == SOCKET_ERROR)
			{
				int err = SOCKETERRNO;
				SDL_Log("UDP, setsockopt: %s\n", socketerror(err));
				return -1;
			}
			netv4_broadcastsocket = socketid;

			return 0;
		}

		//=============================================================================

		int WINIPv4_Broadcast(sys_socket_t socketid, byte* buf, int len)
		{
			int ret;

			if (socketid != netv4_broadcastsocket)
			{
				if (netv4_broadcastsocket != INVALID_SOCKET)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Attempted to use multiple broadcasts sockets");
				WINIPv4_GetLocalAddress();
				ret = WINS_MakeSocketBroadcastCapable(socketid);
				if (ret == -1)
				{
					SDL_Log("Unable to make socket broadcast capable\n");
					return ret;
				}
			}

			return WINS_Write(socketid, buf, len, (struct qsockaddr*)&broadcastaddrv4);
		}

		//=============================================================================

		int WINS_Write(sys_socket_t socketid, byte* buf, int len, struct qsockaddr* addr)
		{
			int ret;

			ret = sendto(socketid, (char*)buf, len, 0, (struct sockaddr*)addr, sizeof(struct qsockaddr));
			if (ret == SOCKET_ERROR)
			{
				int err = SOCKETERRNO;
				if (err == NET_EWOULDBLOCK)
					return 0;
				SDL_Log("WINS_Write, sendto: %s\n", socketerror(err));
			}
			return ret;
		}

		//=============================================================================

		unsigned short ntohs_v6word(struct qsockaddr* addr, int wordnum)
		{
			unsigned char* ptr = ((struct sockaddr_in6*)addr)->sin6_addr.s6_addr + wordnum * 2;
			return (unsigned short)(ptr[0] << 8) | ptr[1];
		}

		const char* WINS_AddrToString(struct qsockaddr* addr, bool masked)
		{
			static char buffer[64];
			int			haddr;

#ifdef IPPROTO_IPV6
			if (addr->qsa_family == AF_INET6)
			{
				if (masked)
				{
					q_snprintf(
						buffer, sizeof(buffer), "[%x:%x:%x:%x::]/64", ntohs_v6word(addr, 0), ntohs_v6word(addr, 1), ntohs_v6word(addr, 2), ntohs_v6word(addr, 3));
				}
				else
				{
					if (((struct sockaddr_in6*)addr)->sin6_scope_id)
					{
						q_snprintf(
							buffer, sizeof(buffer), "[%x:%x:%x:%x:%x:%x:%x:%x%%%i]:%d", ntohs_v6word(addr, 0), ntohs_v6word(addr, 1), ntohs_v6word(addr, 2),
							ntohs_v6word(addr, 3), ntohs_v6word(addr, 4), ntohs_v6word(addr, 5), ntohs_v6word(addr, 6), ntohs_v6word(addr, 7),
							(int)((struct sockaddr_in6*)addr)->sin6_scope_id, ntohs(((struct sockaddr_in6*)addr)->sin6_port));
					}
					else
					{
						q_snprintf(
							buffer, sizeof(buffer), "[%x:%x:%x:%x:%x:%x:%x:%x]:%d", ntohs_v6word(addr, 0), ntohs_v6word(addr, 1), ntohs_v6word(addr, 2),
							ntohs_v6word(addr, 3), ntohs_v6word(addr, 4), ntohs_v6word(addr, 5), ntohs_v6word(addr, 6), ntohs_v6word(addr, 7),
							ntohs(((struct sockaddr_in6*)addr)->sin6_port));
					}
				}
			}
			else
#endif
			{
				haddr = ntohl(((struct sockaddr_in*)addr)->sin_addr.s_addr);
				if (masked)
				{
					q_snprintf(buffer, sizeof(buffer), "%d.%d.%d.0/24", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff, (haddr >> 8) & 0xff);
				}
				else
				{
					q_snprintf(
						buffer, sizeof(buffer), "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff,
						ntohs(((struct sockaddr_in*)addr)->sin_port));
				}
			}
			return buffer;
		}

		//=============================================================================

		int WINIPv4_StringToAddr(const char* string, struct qsockaddr* addr)
		{
			int ha1, ha2, ha3, ha4, hp, ipaddr;

			sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
			ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

			addr->qsa_family = AF_INET;
			((struct sockaddr_in*)addr)->sin_addr.s_addr = htonl(ipaddr);
			((struct sockaddr_in*)addr)->sin_port = htons((unsigned short)hp);
			return 0;
		}

		//=============================================================================

		int WINS_GetSocketAddr(sys_socket_t socketid, struct qsockaddr* addr)
		{
			socklen_t addrlen = sizeof(struct qsockaddr);

			memset(addr, 0, sizeof(struct qsockaddr));
			getsockname(socketid, (struct sockaddr*)addr, &addrlen);

			if (addr->qsa_family == AF_INET)
			{
				in_addr_t a = ((struct sockaddr_in*)addr)->sin_addr.s_addr;
				if (a == 0 || a == htonl(INADDR_LOOPBACK))
					((struct sockaddr_in*)addr)->sin_addr.s_addr = myAddrv4;
			}
#ifdef IPPROTO_IPV6
			if (addr->qsa_family == AF_INET6)
			{
				static const in_addr6_t _in6addr_any; // = IN6ADDR_ANY_INIT;
				if (!memcmp(&((struct sockaddr_in6*)addr)->sin6_addr, &_in6addr_any, sizeof(in_addr6_t)))
					memcpy(&((struct sockaddr_in6*)addr)->sin6_addr, &myAddrv6, sizeof(in_addr6_t));
			}
#endif

			return 0;
		}

		//=============================================================================

		int WINIPv4_GetNameFromAddr(struct qsockaddr* addr, char* name)
		{
			struct hostent* hostentry;

			hostentry = gethostbyaddr((char*)&((struct sockaddr_in*)addr)->sin_addr, sizeof(struct in_addr), AF_INET);
			if (hostentry)
			{
				strncpy(name, (char*)hostentry->h_name, NET_NAMELEN - 1);
				return 0;
			}

			strcpy(name, WINS_AddrToString(addr, false));
			return 0;
		}

		int WINIPv4_GetAddresses(qhostaddr_t* addresses, int maxaddresses)
		{
			int				result = 0, b;
			struct hostent* h;

			if (bindAddrv4 == INADDR_ANY)
			{
				// on windows, we can just do a dns lookup on our own hostname and expect an ipv4 result
				char   buf[64];
				u_long addr;
				gethostname(buf, sizeof(buf));

				h = gethostbyname(buf);
				if (h && h->h_addrtype == AF_INET)
				{
					for (b = 0; h->h_addr_list[b] && result < maxaddresses; b++)
					{
						addr = ntohl(*(in_addr_t*)h->h_addr_list[b]);
						q_snprintf(
							addresses[result++], sizeof(addresses[0]), "%ld.%ld.%ld.%ld", (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);
					}
				}
			}

			if (!result)
				q_strlcpy(addresses[result++], engine->net->my_ipv4_address, sizeof(addresses[0]));
			return result;
		}
		int WINIPv6_GetAddresses(qhostaddr_t* addresses, int maxaddresses)
		{
			int result = 0;

			//	if (bindAddrv6 == IN6ADDR_ANY_INIT)
			{
				// on windows, we can just do a dns lookup on our own hostname and expect an ipv4 result
				struct addrinfo hints, * addrlist, * itr;
				char			buf[64];
				memset(&hints, 0, sizeof(struct addrinfo));
				hints.ai_family = AF_INET6;		/* Allow IPv4 or IPv6 */
				hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
				hints.ai_flags = 0;
				hints.ai_protocol = 0; /* Any protocol */

				gethostname(buf, sizeof(buf));
				if (qgetaddrinfo(buf, NULL, &hints, &addrlist) == 0)
				{
					for (itr = addrlist; itr && result < maxaddresses; itr = itr->ai_next)
					{
						if (itr->ai_addr->sa_family == AF_INET6)
							q_strlcpy(addresses[result++], WINS_AddrToString((struct qsockaddr*)itr->ai_addr, false), sizeof(addresses[0]));
					}
					freeaddrinfo(addrlist);
				}
			}

			if (!result)
				q_strlcpy(addresses[result++], my_ipv6_address, sizeof(addresses[0]));
			return result;
		}

		//=============================================================================

		int WINIPv4_GetAddrFromName(const char* name, struct qsockaddr* addr)
		{
			struct hostent* hostentry;
			char* colon;
			unsigned short	port = net_hostport;

			if (name[0] >= '0' && name[0] <= '9')
				return PartialIPAddress(name, addr);

			colon = strrchr(name, ':');
			if (colon)
			{
				char dupe[MAXHOSTNAMELEN];
				if (colon - name + 1 > MAXHOSTNAMELEN)
					return -1;
				memcpy(dupe, name, colon - name);
				dupe[colon - name] = 0;
				if (strchr(dupe, ':'))
					return -1; // don't resolve a name to an ipv4 address if it has multiple colons in it. its probably an ipx or ipv6 address, and I'd rather not
				// block on any screwed dns resolves
				hostentry = gethostbyname(dupe);
				port = strtoul(colon + 1, NULL, 10);
			}
			else
				hostentry = gethostbyname(name);
			if (!hostentry)
				return -1;

			addr->qsa_family = AF_INET;
			((struct sockaddr_in*)addr)->sin_port = htons(port);
			((struct sockaddr_in*)addr)->sin_addr.s_addr = *(in_addr_t*)hostentry->h_addr_list[0];

			return 0;
		}

		//=============================================================================

		int WINS_AddrCompare(struct qsockaddr* addr1, struct qsockaddr* addr2)
		{
			if (addr1->qsa_family != addr2->qsa_family)
				return -1;

#ifdef IPPROTO_IPV6
			if (addr1->qsa_family == AF_INET6)
			{
				if (memcmp(&((struct sockaddr_in6*)addr1)->sin6_addr, &((struct sockaddr_in6*)addr2)->sin6_addr, sizeof(((struct sockaddr_in6*)addr2)->sin6_addr)))
					return -1;

				if (((struct sockaddr_in6*)addr1)->sin6_port != ((struct sockaddr_in6*)addr2)->sin6_port)
					return 1;
				if (((struct sockaddr_in6*)addr1)->sin6_scope_id && ((struct sockaddr_in6*)addr2)->sin6_scope_id &&
					((struct sockaddr_in6*)addr1)->sin6_scope_id !=
					((struct sockaddr_in6*)addr2)->sin6_scope_id) // the ipv6 scope id is for use with link-local addresses, to identify the specific interface.
					return 1;
			}
			else
#endif
			{
				if (((struct sockaddr_in*)addr1)->sin_addr.s_addr != ((struct sockaddr_in*)addr2)->sin_addr.s_addr)
					return -1;

				if (((struct sockaddr_in*)addr1)->sin_port != ((struct sockaddr_in*)addr2)->sin_port)
					return 1;
			}

			return 0;
		}

		//=============================================================================

		int WINS_GetSocketPort(struct qsockaddr* addr)
		{
#ifdef IPPROTO_IPV6
			if (addr->qsa_family == AF_INET6)
				return ntohs(((struct sockaddr_in6*)addr)->sin6_port);
			else
#endif
				return ntohs(((struct sockaddr_in*)addr)->sin_port);
		}

		int WINS_SetSocketPort(struct qsockaddr* addr, int port)
		{
#ifdef IPPROTO_IPV6
			if (addr->qsa_family == AF_INET6)
				((struct sockaddr_in6*)addr)->sin6_port = htons((unsigned short)port);
			else
#endif
				((struct sockaddr_in*)addr)->sin_port = htons((unsigned short)port);
			return 0;
		}

		//=============================================================================

#ifdef IPPROTO_IPV6
// winxp (and possibly win2k) is dual stack.
// vista+ has a hybrid stack

		static void WINIPv6_GetLocalAddress(void)
		{
			char			buff[MAXHOSTNAMELEN];
			int				err;
			struct addrinfo hints, * local = NULL;

			//	if (myAddrv6 != IN6ADDR_ANY)
			//		return;

			if (gethostname(buff, MAXHOSTNAMELEN) == SOCKET_ERROR)
			{
				err = SOCKETERRNO;
				SDL_Log("WINIPv6_GetLocalAddress: gethostname failed (%s)\n", socketerror(err));
				return;
			}
			buff[MAXHOSTNAMELEN - 1] = 0;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			if (qgetaddrinfo && qgetaddrinfo(buff, NULL, &hints, &local) == 0)
			{
				size_t l;
				q_strlcpy(my_ipv6_address, WINS_AddrToString((struct qsockaddr*)local->ai_addr, false), sizeof(my_ipv6_address));
				l = strlen(my_ipv6_address);
				if (l > 2 && !strcmp(my_ipv6_address + l - 2, ":0"))
					my_ipv6_address[l - 2] = 0;
				qfreeaddrinfo(local);
			}
			err = WSAGetLastError();

			if (local == NULL)
			{
				SDL_Log("WINIPv6_GetLocalAddress: gethostbyname failed (%s)\n", __WSAE_StrError(err));
				return;
			}
		}

		sys_socket_t WINIPv6_Init(void)
		{
			int	 i;
			char buff[MAXHOSTNAMELEN];

			if (COM_CheckParm("-noudp") || COM_CheckParm("-noudp6"))
				return -1;

			qgetaddrinfo = (void*)GetProcAddress(GetModuleHandle("ws2_32.dll"), "getaddrinfo");
			qfreeaddrinfo = (void*)GetProcAddress(GetModuleHandle("ws2_32.dll"), "freeaddrinfo");
			if (!qgetaddrinfo || !qfreeaddrinfo)
			{
				qgetaddrinfo = NULL;
				qfreeaddrinfo = NULL;
				SDL_Log("Winsock lacks getaddrinfo, ipv6 support is unavailable.\n");
				return INVALID_SOCKET;
			}

			if (winsock_initialized == 0)
			{
				int err = WSAStartup(MAKEWORD(2, 2), &winsockdata);
				if (err != 0)
				{
					SDL_Log("Winsock initialization failed (%s)\n", socketerror(err));
					return INVALID_SOCKET;
				}
			}
			winsock_initialized++;

			// determine my name & address
			if (gethostname(buff, MAXHOSTNAMELEN) != 0)
			{
				int err = SOCKETERRNO;
				SDL_Log("WINIPv6_Init: gethostname failed (%s)\n", socketerror(err));
			}
			else
			{
				buff[MAXHOSTNAMELEN - 1] = 0;
			}
			i = COM_CheckParm("-ip6");
			if (i)
			{
				if (i < com_argc - 1)
				{
					if (WINIPv6_GetAddrFromName(com_argv[i + 1], (struct qsockaddr*)&bindAddrv6))
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"%s is not a valid IPv6 address", com_argv[i + 1]);
					if (!*my_ipv6_address)
						strcpy(my_ipv6_address, com_argv[i + 1]);
				}
				else
				{
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"WINIPv6_Init: you must specify an IP address after -ip");
				}
			}
			else
			{
				memset(&bindAddrv6, 0, sizeof(bindAddrv6));
				if (!*my_ipv6_address)
				{
					strcpy(my_ipv6_address, "[::]");
					WINIPv6_GetLocalAddress();
				}
			}

			myAddrv6 = bindAddrv6;

			if ((netv6_controlsocket = WINIPv6_OpenSocket(0)) == INVALID_SOCKET)
			{
				SDL_Log("WINIPv6_Init: Unable to open control socket, UDP disabled\n");
				if (--winsock_initialized == 0)
					WSACleanup();
				return INVALID_SOCKET;
			}

			broadcastaddrv6.sin6_family = AF_INET6;
			memset(&broadcastaddrv6.sin6_addr, 0, sizeof(broadcastaddrv6.sin6_addr));
			broadcastaddrv6.sin6_addr.s6_addr[0] = 0xff;
			broadcastaddrv6.sin6_addr.s6_addr[1] = 0x03;
			broadcastaddrv6.sin6_addr.s6_addr[15] = 0x01;
			broadcastaddrv6.sin6_port = htons((unsigned short)net_hostport);

			SDL_Log("IPv6 UDP Initialized\n");
			ipv6Available = true;

			return netv6_controlsocket;
		}

		sys_socket_t WINIPv6_Listen(bool state)
		{
			if (state)
			{
				// enable listening
				if (netv6_acceptsocket == INVALID_SOCKET)
					netv6_acceptsocket = WINIPv6_OpenSocket(net_hostport);
			}
			else
			{
				// disable listening
				if (netv6_acceptsocket != INVALID_SOCKET)
				{
					WINS_CloseSocket(netv6_acceptsocket);
					netv6_acceptsocket = INVALID_SOCKET;
				}
			}
			return netv6_acceptsocket;
		}
		void WINIPv6_Shutdown(void)
		{
			WINIPv6_Listen(false);
			WINS_CloseSocket(netv6_controlsocket);
			if (--winsock_initialized == 0)
				WSACleanup();
		}
		sys_socket_t WINIPv6_OpenSocket(int port)
		{
			sys_socket_t		newsocket;
			struct sockaddr_in6 address;
			u_long				_true = 1;
			int					err;

			if ((newsocket = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
			{
				err = SOCKETERRNO;
				SDL_Log("WINS_OpenSocket: %s\n", socketerror(err));
				return INVALID_SOCKET;
			}

			setsockopt(newsocket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&_true, sizeof(_true));

			if (ioctlsocket(newsocket, FIONBIO, &_true) == SOCKET_ERROR)
				goto ErrorReturn;

			memset(&address, 0, sizeof(address));
			address.sin6_family = AF_INET6;
			address.sin6_addr = bindAddrv6;
			address.sin6_port = htons((unsigned short)port);
			if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == 0)
			{
				// we don't know if we're the server or not. oh well.
				struct ipv6_mreq req;
				req.ipv6mr_multiaddr = broadcastaddrv6.sin6_addr;
				req.ipv6mr_interface = 0;
				setsockopt(newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&req, sizeof(req));

				return newsocket;
			}

			if (ipv6Available)
			{
				err = SOCKETERRNO;
				Con_Warning("Unable to bind to %s (%s)\n", WINS_AddrToString((struct qsockaddr*)&address, false), socketerror(err));
				return INVALID_SOCKET; /* not reached */
			}
			/* else: we are still in init phase, no need to error */

		ErrorReturn:
			err = SOCKETERRNO;
			SDL_Log("WINS_OpenSocket: %s\n", socketerror(err));
			closesocket(newsocket);
			return INVALID_SOCKET;
		}
		sys_socket_t WINIPv6_CheckNewConnections(void)
		{
			char buf[4096];

			if (netv6_acceptsocket == INVALID_SOCKET)
				return INVALID_SOCKET;

			if (recvfrom(netv6_acceptsocket, buf, sizeof(buf), MSG_PEEK, NULL, NULL) != SOCKET_ERROR)
			{
				return netv6_acceptsocket;
			}
			return INVALID_SOCKET;
		}
		int WINIPv6_Broadcast(sys_socket_t socketid, byte* buf, int len)
		{
			broadcastaddrv6.sin6_port = htons((unsigned short)net_hostport);
			return WINS_Write(socketid, buf, len, (struct qsockaddr*)&broadcastaddrv6);
		}
		int WINIPv6_StringToAddr(const char* string, struct qsockaddr* addr)
		{ // This is never actually called...
			return -1;
		}
		int WINIPv6_GetNameFromAddr(struct qsockaddr* addr, char* name)
		{
			// FIXME: should really do a reverse dns lookup.
			q_strlcpy(name, WINS_AddrToString(addr, false), NET_NAMELEN);
			return 0;
		}
		int WINIPv6_GetAddrFromName(const char* name, struct qsockaddr* addr)
		{
			// ipv6 addresses take form of [::1]:26000 or eg localhost:26000. just ::1 is NOT supported, but localhost as-is is okay. [localhost]:26000 is acceptable,
			// but will fail to resolve as ipv4.
			struct addrinfo* addrinfo = NULL;
			struct addrinfo* pos;
			struct addrinfo	 udp6hint;
			int				 error;
			char* port;
			char			 dupbase[256];
			size_t			 len;
			bool		 success = false;

			memset(&udp6hint, 0, sizeof(udp6hint));
			udp6hint.ai_family = 0; // Any... we check for AF_INET6 or 4
			udp6hint.ai_socktype = SOCK_DGRAM;
			udp6hint.ai_protocol = IPPROTO_UDP;

			if (*name == '[')
			{
				port = strstr(name, "]");
				if (!port)
					error = EAI_NONAME;
				else
				{
					len = port - (name + 1);
					if (len >= sizeof(dupbase))
						len = sizeof(dupbase) - 1;
					strncpy(dupbase, name + 1, len);
					dupbase[len] = '\0';
					error = qgetaddrinfo ? qgetaddrinfo(dupbase, (port[1] == ':') ? port + 2 : NULL, &udp6hint, &addrinfo) : EAI_NONAME;
				}
			}
			else
			{
				port = strrchr(name, ':');

				if (port)
				{
					len = port - name;
					if (len >= sizeof(dupbase))
						len = sizeof(dupbase) - 1;
					strncpy(dupbase, name, len);
					dupbase[len] = '\0';
					error = qgetaddrinfo ? qgetaddrinfo(dupbase, port + 1, &udp6hint, &addrinfo) : EAI_NONAME;
				}
				else
					error = EAI_NONAME;
				if (error) // failed, try string with no port.
					error = qgetaddrinfo ? qgetaddrinfo(name, NULL, &udp6hint, &addrinfo)
					: EAI_NONAME; // remember, this func will return any address family that could be using the udp protocol... (ip4 or ip6)
			}

			if (!error)
			{
				((struct sockaddr*)addr)->sa_family = 0;
				for (pos = addrinfo; pos; pos = pos->ai_next)
				{
					if (0) // pos->ai_family == AF_INET)
					{
						memcpy(addr, pos->ai_addr, pos->ai_addrlen);
						success = true;
						break;
					}
					if (pos->ai_family == AF_INET6 && !success)
					{
						memcpy(addr, pos->ai_addr, pos->ai_addrlen);
						success = true;
					}
				}
				freeaddrinfo(addrinfo);
			}

			if (success)
			{
				if (((struct sockaddr*)addr)->sa_family == AF_INET)
				{
					if (!((struct sockaddr_in*)addr)->sin_port)
						((struct sockaddr_in*)addr)->sin_port = htons(net_hostport);
				}
				else if (((struct sockaddr*)addr)->sa_family == AF_INET6)
				{
					if (!((struct sockaddr_in6*)addr)->sin6_port)
						((struct sockaddr_in6*)addr)->sin6_port = htons(net_hostport);
				}
				return 0;
			}
			return -1;
		}
#endif

	};

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
							NET_SendMessage(client->netconnection, &client->message);
							engine->sz->Clear(&client->message);
						}
						else
						{
							NET_GetMessage(client->netconnection);
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
			count = NET_SendToAll(&buf, 5.0);
			if (count)
				SDL_Log("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

			//PR_SwitchQCVM(&sv.qcvm);
			for (i = 0, client = engine->svs.clients; i < engine->svs.maxclients; i++, client++)
				if (client->active)
					engine->server->DropClient(crash);

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
		void DropClient(bool crash)
		{
			int		  saveSelf;
			int		  i;
			client_t* client;

			if (!crash)
			{
				// send any final messages (don't check for errors)
				if (engine->net->CanSendMessage(engine->host->client->netconnection))
				{
					engine->msg->WriteByte(&engine->host->client->message, svc_disconnect);
					engine->net->NET_SendMessage(engine->host->client->netconnection, &engine->host->client->message);
				}

				if (engine->host->client->edict && engine->host->client->spawned)
				{
					// call the prog function for removing a client
					// this will set the body to a dead frame, among other things
					//qcvm_t* oldvm = qcvm;
					//PR_SwitchQCVM(NULL);
					//PR_SwitchQCVM(&sv.qcvm);
					//saveSelf = pr_global_struct->self;
					//pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
					//PR_ExecuteProgram(pr_global_struct->ClientDisconnect);
					//pr_global_struct->self = saveSelf;
					//PR_SwitchQCVM(NULL);
					//PR_SwitchQCVM(oldvm);
				}

				SDL_Log("Client %s removed\n", engine->host->client->name);
			}

			// break the net connection
			engine->net->Close(engine->host->client->netconnection);
			engine->host->client->netconnection = NULL;

			//SVFTE_DestroyFrames(engine->host->client); // release any delta state

			// free the client (the body stays around)
			engine->host->client->active = false;
			engine->host->client->name[0] = 0;
			engine->host->client->old_frags = -999999;
			engine->net->activeconnections--;

			// send notification to all clients
			for (i = 0, client = engine->svs.clients; i < engine->svs.maxclients; i++, client++)
			{
				if (!client->knowntoqc)
					continue;

				engine->msg->WriteByte(&client->message, svc_updatename);
				engine->msg->WriteByte(&client->message, host_client - svs.clients);
				engine->msg->WriteString(&client->message, "");
				engine->msg->WriteByte(&client->message, svc_updatecolors);
				engine->msg->WriteByte(&client->message, host_client - svs.clients);
				engine->msg->WriteByte(&client->message, 0);

				engine->msg->WriteByte(&client->message, svc_updatefrags);
				engine->msg->WriteByte(&client->message, host_client - svs.clients);
				engine->msg->WriteShort(&client->message, 0);
			}
		}


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


		net_landriver_t landrivers[] = {
			{"Winsock TCPIP",
			 false,
			 0,
			 WINIPv4_Init,
			 WINIPv4_Shutdown,
			 WINIPv4_Listen,
			 WINIPv4_GetAddresses,
			 WINIPv4_OpenSocket,
			 WINS_CloseSocket,
			 WINS_Connect,
			 WINIPv4_CheckNewConnections,
			 WINS_Read,
			 WINS_Write,
			 WINIPv4_Broadcast,
			 WINS_AddrToString,
			 WINIPv4_StringToAddr,
			 WINS_GetSocketAddr,
			 WINIPv4_GetNameFromAddr,
			 WINIPv4_GetAddrFromName,
			 WINS_AddrCompare,
			 WINS_GetSocketPort,
			 WINS_SetSocketPort},
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
			{"Winsock IPX",
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
			 WIPX_SetSocketPort} };
		
		int	   numlandrivers;

		qsocket_t* activeSockets = NULL;
		qsocket_t* freeSockets = NULL;
		int		   numsockets = 0;


		
		net_driver_t* drivers = NULL;


		Engine* engine;
		double time;

		qsocket_t* activeSockets = NULL;
		qsocket_t* net_freeSockets = NULL;
		int		   net_numsockets = 0;

		bool ipxAvailable = false;
		bool ipv4Available = false;
		bool ipv6Available = false;

		int net_hostport;
		int DEFAULTnet_hostport = 26000;

		char my_ipx_address[NET_NAMELEN];
		char my_ipv4_address[NET_NAMELEN];
		char my_ipv6_address[NET_NAMELEN];

		bool listening = false;

		bool		  slistInProgress = false;
		bool		  slist_silent = false;
		enum slistScope_e slist_scope = SLIST_LOOP;
		static double	  slistStartTime;
		static double	  slistActiveTime;
		static int		  slistLastShown;

		static void			 Slist_Send(void*,Engine* engine);
		static void			 Slist_Poll(void*,Engine* engine);
		PollProcedure slistSendProcedure = { NULL, 0.0, Slist_Send };
		PollProcedure slistPollProcedure = { NULL, 0.0, Slist_Poll };

		sizebuf_t message;
		int		  activeconnections = 0;

		int messagesSent = 0;
		int messagesReceived = 0;
		int unreliableMessagesSent = 0;
		int unreliableMessagesReceived = 0;

		cvar_t messagetimeout = { "engine->net->messagetimeout", "300", CVAR_NONE };
		cvar_t connecttimeout = { "net_connecttimeout", "10", CVAR_NONE }; // this might be a little brief, but we don't have a way to protect against smurf attacks.
		cvar_t hostname = { "hostname", "UNNAMED", CVAR_SERVERINFO };

		int		driverlevel;


		NET(Engine* e) {
			engine = e;
			engine->net = this;
		}

		double SetNetTime(void)
		{
			time = DoubleTime();
			return time;
		}

		qsocket_t* NewQSocket(void)
		{
			qsocket_t* sock;

			if (net_freeSockets == NULL)
				return NULL;

			if (activeconnections >= engine->svs.maxclients)
				return NULL;

			// get one from free list
			sock = net_freeSockets;
			net_freeSockets = sock->next;

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
			sock->next = net_freeSockets;
			net_freeSockets = sock;
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
			sfunc.Close(sock);

			FreeQSocket(sock);
		}


		bool CanSendMessage(qsocket_t* sock)
		{
			if (!sock)
				return false;

			if (sock->disconnected)
				return false;

			SetNetTime();

			return sfunc.CanSendMessage(sock);
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
			r = sfunc.QSendMessage(sock, data);
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
			r = sfunc.SendUnreliableMessage(sock, data);
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
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"MSG_WriteByte: range error");
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
	Loop* loop = nullptr;
	MSG* msg = nullptr;
	Datagram* datagram = nullptr;

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
		loop = new Loop(this);
		msg = new MSG(this);
		datagram = new Datagram(this);

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