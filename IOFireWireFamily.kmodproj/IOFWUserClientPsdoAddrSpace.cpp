/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and 
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").	You may not use this file except in compliance with the
 * License.	 Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *	IOFWUserClientPseudoAddressSpace.cpp
 *	IOFireWireFamily
 *
 *	Created by NWG on Wed Dec 06 2000.
 *	Copyright (c) 2000 Apple, Inc. All rights reserved.
 *
 */

#ifndef __IOFWUserClientPseuAddrSpace_H__
#define __IOFWUserClientPseuAddrSpace_H__

#include <IOKit/assert.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOUserClient.h>

#include <IOKit/firewire/IOFireWireController.h>
#include <IOKit/firewire/IOFireWireUserClient.h>
#include <IOKit/firewire/IOFWUserClientPsdoAddrSpace.h>

// ============================================================
//
// IOFWPacketHeader
//
// ============================================================

IOFWPacketHeader_t::IOFWPacketHeader_t()
{
	CommonHeader.type			= IOFWPacketHeader::kFree ;
	CommonHeader.next			= this ;
	IOFWPacketHeaderGetSize(this)	= 0 ;
	IOFWPacketHeaderGetOffset(this) = 0 ;
}

inline IOByteCount& IOFWPacketHeaderGetSize(IOFWPacketHeader_t* hdr)
{
	return hdr->CommonHeader.args[1] ;
}

inline IOByteCount& IOFWPacketHeaderGetOffset(IOFWPacketHeader_t* hdr)
{
	return hdr->CommonHeader.args[2] ;
}

inline void InitIncomingPacketHeader(
	IOFWPacketHeader_t*				header,
	IOFWPacketHeader_t*				next,
	const IOByteCount				len,
	const IOByteCount				offset,
	OSAsyncReference*				ref,
	void*							refCon,
	UInt16							nodeID,
	const IOFWSpeed&   				speed,
	const FWAddress&				addr,
	const Boolean					lockWrite)
{
	header->CommonHeader.type				= IOFWPacketHeader::kIncomingPacket ;
	header->CommonHeader.next				= next ;
	IOFWPacketHeaderGetSize(header)			= len ;
	IOFWPacketHeaderGetOffset(header)		= offset ;
	header->CommonHeader.whichAsyncRef		= ref ;
	header->CommonHeader.argCount			= 8;

	header->IncomingPacket.commandID		= (UInt32) header ;
	header->IncomingPacket.nodeID			= nodeID ;
	header->IncomingPacket.speed			= speed ;
	header->IncomingPacket.addrHi			= addr.addressHi;
	header->IncomingPacket.addrLo			= addr.addressLo;
	header->IncomingPacket.lockWrite		= lockWrite ;
}

inline void InitSkippedPacketHeader(
	IOFWPacketHeader*				header,
	IOFWPacketHeader*				next,
	const IOByteCount				offset,
	OSAsyncReference*				ref,
	void*							refCon)
{
	header->CommonHeader.type 				= IOFWPacketHeader::kSkippedPacket ;
	header->CommonHeader.next				= next ;
	IOFWPacketHeaderGetSize(header)			= 0;
	IOFWPacketHeaderGetOffset(header)		= offset ;
	header->CommonHeader.whichAsyncRef		= ref ;
	header->CommonHeader.argCount			= 2;
	
	header->SkippedPacket.commandID			= (UInt32) header ;
	header->SkippedPacket.skippedPacketCount= 1;
}

inline void InitReadPacketHeader(
	IOFWPacketHeader*				header,
	IOFWPacketHeader*				next,
	IOByteCount						len,
	IOByteCount						offset,
	OSAsyncReference*				ref,
	void*							refCon,
	UInt16							nodeID,
	IOFWSpeed&						speed,
	FWAddress						addr,
	IOFWRequestRefCon				reqrefcon)
{
	header->CommonHeader.type			= IOFWPacketHeader::kReadPacket ;
	header->CommonHeader.next			= next ;
	IOFWPacketHeaderGetSize(header)		= len ;
	IOFWPacketHeaderGetOffset(header)	= offset ;
	header->CommonHeader.whichAsyncRef	= ref ;
	header->CommonHeader.argCount		= 7 ;

	header->ReadPacket.commandID		= (UInt32) header ;
	header->ReadPacket.nodeID			= nodeID ;
	header->ReadPacket.speed			= speed ;
	header->ReadPacket.addrHi   		= addr.addressHi ;
	header->ReadPacket.addrLo			= addr.addressLo ;
}

inline Boolean IsSkippedPacketHeader(
	IOFWPacketHeader*				header)
{
	return header->CommonHeader.type == IOFWPacketHeader::kSkippedPacket ;
}

inline Boolean IsFreePacketHeader(
	IOFWPacketHeader*				header)
{
	return header->CommonHeader.type == IOFWPacketHeader::kFree ;
}

inline Boolean IsReadPacketHeader(
	IOFWPacketHeader*				header)
{
	return header->CommonHeader.type == IOFWPacketHeader::kReadPacket ;
}

//////////////////////////////////////////////////////////////
//
//	IOFWUserClientPseudoAddrSpace methods
//
//////////////////////////////////////////////////////////////

OSDefineMetaClassAndStructors(IOFWUserClientPseudoAddrSpace, IOFWPseudoAddressSpace) ;

bool
IOFWUserClientPseudoAddrSpace::initAll(
	IOFireWireUserClient*		inUserClient,
	IOMemoryDescriptor*			inPacketQueueBuffer,
	IOMemoryDescriptor*			inBackingStore,
	UInt32						inUserRefCon,
	IOFireWireController*		control,
	FWAddress*					addr,
	UInt32						len,
	FWReadCallback				reader,
	FWWriteCallback				writer,
	void*						refcon)
{
	fLock = IOLockAlloc() ;
	if (!fLock)
		return false ;

	fPacketQueueBuffer			= NULL ;
	fBackingStore				= inBackingStore ;
	fUserRefCon					= inUserRefCon ;
	fLastWrittenHeader			= NULL ;
	fLastReadHeader				= NULL ;
	fWaitingForUserCompletion	= false ;
	fBufferAvailable			= 0 ;

	bool statusFlag = (inUserClient && inPacketQueueBuffer) ;

	if (statusFlag = IOFWPseudoAddressSpace::initAll((IOFireWireBus*)control, addr, len, reader, writer, this))
	{
		fUserClient					= inUserClient ;
		fUserClient->retain() ;

		fPacketQueueBuffer			= inPacketQueueBuffer ;
		fBufferAvailable			= inPacketQueueBuffer->getLength() ;
		fAddress					= *addr ;

		fSkippedPacketAsyncNotificationRef[0]	= 0 ;
		fPacketAsyncNotificationRef[0]			= 0 ;
	}
	
	return statusFlag ;
}

void
IOFWUserClientPseudoAddrSpace::free()
{
	deactivate() ;

	if (fPacketQueueBuffer)
		fPacketQueueBuffer->release() ;

	fUserClient->release() ;		// we keep a reference to the user client which must be released.
}

void
IOFWUserClientPseudoAddrSpace::deactivate()
{
	// zzz - this should clean up this address space, however there may be issues with this
	// code.

	fBufferAvailable = 0 ;	// zzz do we need locking here to protect our data?
	fLastReadHeader = NULL ;	
	
	IOFWPacketHeader*	firstHeader = fLastWrittenHeader ;
	IOFWPacketHeader*	tempHeader ;

	if (fLastWrittenHeader)
	{
		while (fLastWrittenHeader->CommonHeader.next != firstHeader)
		{
			tempHeader = fLastWrittenHeader->CommonHeader.next ;
//			delete fLastWrittenHeader ;
			IOFree(fLastWrittenHeader, sizeof(*fLastWrittenHeader)) ;
			fLastWrittenHeader = tempHeader ;	
		}
	
//		delete fLastWrittenHeader ;
		IOFree(fLastWrittenHeader, sizeof(*fLastWrittenHeader)) ;
	}
	
	IOFWPseudoAddressSpace::deactivate() ;
}

UInt32
IOFWUserClientPseudoAddrSpace::pseudoAddrSpaceReader(
	void*					refCon,
	UInt16					nodeID,
	IOFWSpeed&				speed,
	FWAddress				addr,
	UInt32					len,
	IOMemoryDescriptor**	buf,
	IOByteCount*			offset,
	IOFWRequestRefCon		reqrefcon)
{
	IOFWUserClientPseudoAddrSpace*	me = OSDynamicCast(IOFWUserClientPseudoAddrSpace, (IOService*)refCon) ;

	assert(me) ;

	// create next header if it doesn't exist...
	if (!me->fLastWrittenHeader)
		me->fLastReadHeader = me->fLastWrittenHeader = new IOFWPacketHeader ;

	IOFWPacketHeader*	currentHeader = me->fLastWrittenHeader ;

	// zzz looks like for now the way we determine whether the response should be automatic
	// or not is by checking whether or not we have a backing store. We should probably
	// check a flag which is set on creation of this address space instead...

	UInt32	resultCode = kFWResponseComplete ;
	if (me->fBackingStore)
	{
		*buf = me->fBackingStore ;
		*offset = /*((addr.addressHi - me->fAddress.addressHi) << 32)*/ + (addr.addressLo - me->fAddress.addressLo) ;
		// hi difference to be added back when IOByteCount becomes 64 bit.
	}
	else
		if (!me->fReadAsyncNotificationRef[0])
		{
			resultCode	= kFWResponseTypeError ;
		}
		else	// we have an async ref for reading and the backing store is nil,
				// so we post notification to user space for the read results
		{
			if (!IsFreePacketHeader(currentHeader))
			{
				if ( (currentHeader->CommonHeader.next == currentHeader)	// packet insertion code
					 || (!IsFreePacketHeader(currentHeader->CommonHeader.next) ))
				{
					IOFWPacketHeader*	newHeader		= new IOFWPacketHeader ;
					newHeader->CommonHeader.next		= currentHeader->CommonHeader.next ;
					currentHeader->CommonHeader.next	= newHeader ;
					currentHeader						= currentHeader->CommonHeader.next ;
				}
				else
				{
					currentHeader = currentHeader->CommonHeader.next ;
				}
			}

			// save info in header
			InitReadPacketHeader(
					currentHeader,
					currentHeader->CommonHeader.next,
					len,
					*offset,
					& (me->fPacketAsyncNotificationRef),
					(void*) currentHeader,//me->fUserRefCon,	// zzz don't know what this refcon was used for
					nodeID,
					speed,
					addr,
					reqrefcon) ;
			
			me->sendPacketNotification(currentHeader) ;

		}
	return	resultCode ;
}

UInt32
IOFWUserClientPseudoAddrSpace::pseudoAddrSpaceWriter(
	void*					refCon,
	UInt16					nodeID,
	IOFWSpeed&				speed,
	FWAddress				addr,
	UInt32					len,
	const void*				buf,
	IOFWRequestRefCon		reqrefcon)
{
	IOFWUserClientPseudoAddrSpace*	me = OSDynamicCast(IOFWUserClientPseudoAddrSpace, (IOService*)refCon) ;

	assert(me) ;

	IOByteCount		spaceAtEnd	= me->fPacketQueueBuffer->getLength() ;
	IOByteCount		destOffset	= 0 ;
	bool			wontFit		= false ;

	IOLockLock(me->fLock) ;

	// create next header if it doesn't exist...
	if (!me->fLastWrittenHeader)
		me->fLastReadHeader = me->fLastWrittenHeader = new IOFWPacketHeader ;


	spaceAtEnd -= (IOFWPacketHeaderGetOffset(me->fLastWrittenHeader)
					 + IOFWPacketHeaderGetSize(me->fLastWrittenHeader)) ;

	if (me->fBufferAvailable < len)
		wontFit = true ;
	else
	{
		if (len <= spaceAtEnd)
			destOffset = IOFWPacketHeaderGetOffset(me->fLastWrittenHeader) + IOFWPacketHeaderGetSize(me->fLastWrittenHeader) ;
		else
		{
			if ( (len + spaceAtEnd) <= me->fBufferAvailable )
				destOffset = 0 ;
			else
			{
				destOffset = IOFWPacketHeaderGetOffset(me->fLastWrittenHeader) ;
			 wontFit = true ;
			}
		}
	}
	
	IOFWPacketHeader*	currentHeader = me->fLastWrittenHeader ;

	if (wontFit)
	{
		if (IsSkippedPacketHeader(currentHeader))
			currentHeader->SkippedPacket.skippedPacketCount++ ;
		else
		{
			if (!IsFreePacketHeader(currentHeader))
			{
				if ( (currentHeader->CommonHeader.next == currentHeader)
					 || (!IsFreePacketHeader(currentHeader->CommonHeader.next) ))
				{
					IOFWPacketHeader*	newHeader = new IOFWPacketHeader ;
					newHeader->CommonHeader.next = currentHeader->CommonHeader.next ;
					currentHeader->CommonHeader.next = newHeader ;
					currentHeader = currentHeader->CommonHeader.next ;
				}
				else
				{
					currentHeader = currentHeader->CommonHeader.next ;
				}
			}

			InitSkippedPacketHeader(
					currentHeader,
					currentHeader->CommonHeader.next,
					destOffset,
					& (me->fSkippedPacketAsyncNotificationRef),
					(void*) currentHeader ) ;

	   }

	}
	else
	{
		if (!IsFreePacketHeader(currentHeader))
		{
			if ( (currentHeader->CommonHeader.next == currentHeader)
				 || (!IsFreePacketHeader(currentHeader->CommonHeader.next) ))
			{
				IOFWPacketHeader*	newHeader		= new IOFWPacketHeader ;
				newHeader->CommonHeader.next		= currentHeader->CommonHeader.next ;
				currentHeader->CommonHeader.next	= newHeader ;
				currentHeader						= currentHeader->CommonHeader.next ;
			}
			else
			{
				currentHeader = currentHeader->CommonHeader.next ;
			}
		}

		// save info in header
		InitIncomingPacketHeader(
				currentHeader,
				currentHeader->CommonHeader.next,
				len,
				destOffset,
				& (me->fPacketAsyncNotificationRef),
				(void*) currentHeader,//me->fUserRefCon,	// zzz don't know what this refcon was used for
				nodeID,
				speed,
				addr,
				me->fControl->isLockRequest(reqrefcon)) ;

		// write packet to backing store
		me->fPacketQueueBuffer->prepare(kIODirectionOut) ;
		me->fPacketQueueBuffer->writeBytes(destOffset, buf, len) ;
		me->fPacketQueueBuffer->complete(kIODirectionOut) ;

		me->fBufferAvailable -= len ;
		me->fLastWrittenHeader = currentHeader ;
		
	}
	
	me->sendPacketNotification(currentHeader) ;

	IOLockUnlock(me->fLock) ;

	return kFWResponseComplete ;
}

void
IOFWUserClientPseudoAddrSpace::setAsyncRef_Packet(
	OSAsyncReference	asyncRef)
{
	bcopy(asyncRef, fPacketAsyncNotificationRef, sizeof(OSAsyncReference)) ;	
}

void
IOFWUserClientPseudoAddrSpace::setAsyncRef_SkippedPacket(
	OSAsyncReference	asyncRef)
{
	bcopy(asyncRef, fSkippedPacketAsyncNotificationRef, sizeof(OSAsyncReference)) ;
}

void
IOFWUserClientPseudoAddrSpace::setAsyncRef_Read(
	OSAsyncReference	asyncRef)
{
	bcopy(asyncRef, fReadAsyncNotificationRef, sizeof(OSAsyncReference)) ;
}

void
IOFWUserClientPseudoAddrSpace::clientCommandIsComplete(
	FWClientCommandID inCommandID)
{
	if ( fWaitingForUserCompletion )
	{
		IOLockLock(fLock) ;
		
		IOFWPacketHeader*			oldHeader = fLastReadHeader ;
		IOFWPacketHeader::QueueTag	type = oldHeader->CommonHeader.type ;

		fLastReadHeader = fLastReadHeader->CommonHeader.next ;
				
		switch(type)
		{
			case IOFWPacketHeader::kIncomingPacket:
					fBufferAvailable += oldHeader->IncomingPacket.packetSize ;
					break ;
			default:
					break ;
		}
		
		oldHeader->CommonHeader.type = IOFWPacketHeader::kFree ;
		fWaitingForUserCompletion = false ;

		if ( fLastReadHeader->CommonHeader.type != IOFWPacketHeader::kFree )
			sendPacketNotification(fLastReadHeader) ;
		
		IOLockUnlock(fLock) ;
	}
	else
		IOLog("IOFWUserClientPseudoAddrSpace::ClientCommandIsComplete: WARNING: called with no outstanding transactions!\n") ;
}

void
IOFWUserClientPseudoAddrSpace::sendPacketNotification(
	IOFWPacketHeader*	inPacketHeader)
{
	if (!fWaitingForUserCompletion)
		if (inPacketHeader->CommonHeader.whichAsyncRef[0] != 0)
		{
			IOFireWireUserClient::sendAsyncResult(*(inPacketHeader->CommonHeader.whichAsyncRef),
							kIOReturnSuccess,
							(void**)inPacketHeader->CommonHeader.args,
							inPacketHeader->CommonHeader.argCount) ;
			fWaitingForUserCompletion = true ;
		}
}

#endif //__IOFWUserClientPseuAddrSpace_H__
