
#include "ReceiveRegisters.hh"
#include "DataBlock.hh"
#include "NowindHostSupport.hh"

#include <cassert>

#define DBERR nwhSupport->debugMessage

namespace nwhost {

ReceiveRegisters::ReceiveRegisters()
{
    buffer.resize(7);
}

void ReceiveRegisters::initialize(NowindHostSupport* aSupport)
{
    nwhSupport = aSupport;
}

ReceiveRegisters::~ReceiveRegisters()
{
}

bool ReceiveRegisters::isDone() const
{
    return done;
}

void ReceiveRegisters::clear()
{
    buffer.clear();
}

void ReceiveRegisters::setA(byte data)
{
    buffer[0] = data;
}
void ReceiveRegisters::setBC(word data)
{
    buffer[1] = data & 0xff;
    buffer[2] = data >> 8;
}
void ReceiveRegisters::setDE(word data)
{
    buffer[3] = data & 0xff;
    buffer[4] = data >> 8;
}
void ReceiveRegisters::setHL(word data)
{
    buffer[5] = data & 0xff;
    buffer[6] = data >> 8;
}

void ReceiveRegisters::init()
{
    DBERR("ReceiveRegisters::init()");
    transferSize = buffer.size();  // hardcoded to A+BC+DE+HL = 7 bytes (F used for return value carry == error)
    done = false;
   
    bool byteInUse[256];    // 'byte in use' map
    for (int i=0;i<256;i++)
    {
        byteInUse[i] = false;
    }
    for (unsigned int i=0;i<transferSize;i++)
    {
        byte currentByte = buffer[i];
        byteInUse[currentByte] = true;
    }      
    for (int i=0; i<256; i++)
    {
        if (byteInUse[i] == false)
        {
            // found our header (first byte not 'used' by the data)
            header = i;
            break;
        }
    }
    sendData();
}

void ReceiveRegisters::sendData()
{
    DBERR("ReceiveRegisters::SendData\n");
    
    nwhSupport->sendHeader();
    nwhSupport->send(header);   // data header
    for (unsigned int i=0; i<transferSize; i++)
    {
        byte currentByte = buffer[i];
        nwhSupport->send(currentByte);
    }      
    nwhSupport->send(header);   // data tail
}

void ReceiveRegisters::ack(byte tail)
{
    if (header == tail)
    {		
		DBERR("ACK, tail matched\n");
		done = true;
    }
    else
    {
        static int errors = 0;
        errors++;
        DBERR("ACK, receiveRegisters failed! (errors: %u, tail: 0x%02x)\n", errors, tail);
        sendData(); // resend
    }
}

} // namespace nwhost
