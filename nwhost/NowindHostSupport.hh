

void NowindHost::debugMessage(const char *, ...) const
{
}

void NowindHost::setAllowOtherDiskroms(bool allow)
{
	allowOtherDiskroms = allow;
}
bool NowindHost::getAllowOtherDiskroms() const
{
	return allowOtherDiskroms;
}

void NowindHost::setEnablePhantomDrives(bool enable)
{
	enablePhantomDrives = enable;
}
bool NowindHost::getEnablePhantomDrives() const
{
	return enablePhantomDrives;
}

void NowindHost::setEnableMSXDOS2(bool enable)
{
	enableMSXDOS2 = enable;
}

byte NowindHost::peek() const
{
	return isDataAvailable() ? hostToMsxFifo.front() : 0xFF;
}

// receive:  msx <- pc
byte NowindHost::read()
{
	if (!isDataAvailable()) {
		return 0xff;
	}
	byte result = hostToMsxFifo.front();
	hostToMsxFifo.pop_front();
	return result;
}

bool NowindHost::isDataAvailable() const
{
	return !hostToMsxFifo.empty();
}


void NowindHost::msxReset()
{
	for (unsigned i = 0; i < MAX_DEVICES; ++i) {
		devices[i].fs.reset();
	}
	DBERR("MSX reset\n");
}

SectorMedium* NowindHost::getDisk()
{
	byte num = cmdData[7]; // reg_a
	if (num >= drives.size()) {
		DBERR("MSX requested non-existing drive, reg_a: 0x%02x (ignored)\n", num);
		return 0;
	}
	return drives[num]->getSectorMedium();
}


void NowindHost::auxIn()
{
	char input;
	DBERR("auxIn\n");
	sendHeader();

	dumpRegisters();
	std::cin >> input;

	sendHeader();
	send(input);
    DBERR("auxIn returning 0x%02x\n", input);
}

void NowindHost::auxOut()
{
	DBERR("auxOut: %c\n", cmdData[7]);
	dumpRegisters();
	printf("%c", cmdData[7]);
}

void NowindHost::dumpRegisters()
{
	//reg_[cbedlhfa] + cmd
	DBERR("AF: 0x%04X, BC: 0x%04X, DE: 0x%04X, HL: 0x%04X, CMD: 0x%02X\n", cmdData[7] * 256 + cmdData[6], cmdData[1] * 256 + cmdData[0], cmdData[3] * 256 + cmdData[2], cmdData[5] * 256 + cmdData[4], cmdData[8]);
}

// send:  pc -> msx
void NowindHost::send(byte value)
{
	hostToMsxFifo.push_back(value);
}

void NowindHost::send16(word value)
{
	hostToMsxFifo.push_back(value & 255);
	hostToMsxFifo.push_back(value >> 8);
}

void NowindHost::purge()
{
	hostToMsxFifo.clear();
}

void NowindHost::sendHeader()
{
	send(0xFF); // needed because first read might fail (hardware design choise)!
	send(0xAF);
	send(0x05);
}

void NowindHost::DSKCHG()
{
	SectorMedium* disk = getDisk();
	if (!disk) {
		// no such drive or no disk inserted
		return;
	}

	sendHeader();
	byte num = cmdData[7]; // reg_a
	assert(num < drives.size());
	if (drives[num]->diskChanged()) {
		send(255); // changed
		// read first FAT sector (contains media descriptor)
		byte sectorBuffer[512];
		if (disk->readSectors(sectorBuffer, 1, 1)) {
			// TODO read error
			sectorBuffer[0] = 0;
		}
		send(sectorBuffer[0]); // new mediadescriptor
	} else {
		send(0);   // not changed
		send(255); // dummy
	}
}

void NowindHost::GETDPB()
{
	byte num = cmdData[7]; // reg_a

	DBERR("GETDPB, reg_a: 0x%02X\n", num);
	SectorMedium* disk = getDisk();
	if (!disk) {
		// no such drive or no disk inserted
		DBERR("GETDPB error no disk\n");
		return;
	}

	byte sectorBuffer[512];
	if (disk->readSectors(sectorBuffer, 0, 1)) {
		// TODO read error
		sectorBuffer[0] = 0;
		DBERR("GETDPB error reading sector 0\n");
	}

	// the actual dpb[0] (drive number) is not send
	dpbType dpb;
	word sectorSize = sectorBuffer[12]*256+sectorBuffer[11];	// normally 512 bytes per sector, 4 sectors per cluster

	dpb.ID = sectorBuffer[21];	   	         // offset 1 = 0xF0;  
	dpb.SECSIZ_L = sectorSize & 0xff;	     // offset 2 = 0x00;  
	dpb.SECSIZ_H = sectorSize >> 8;		     // offset 3 = 0x02;  
	dpb.DIRMSK = (sectorSize/32)-1;	         // offset 4 = 0x0F, (SECSIZE/32)-1

	byte dirShift;
	for(dirShift=0;dpb.DIRMSK & (1<<dirShift);dirShift++) {}

	dpb.DIRSHFT = dirShift;		             // offset 5 = 0x04, nr of 1-bits in DIRMSK
	dpb.CLUSMSK = sectorBuffer[13]-1;        // offset 6 = 0x03, nr of (sectors/cluster)-1

	byte cluShift;
	for(cluShift=0;dpb.CLUSMSK & (1<<cluShift);cluShift++) {}

	dpb.CLUSSHFT = cluShift+1;            	 // offset 7 = 0x03, nr of bits in clusterMask+1 

	word firstFATsector = sectorBuffer[15]*256+sectorBuffer[14];

	dpb.FIRFAT_L = firstFATsector & 0xFF;    // offset 8 = 0x01, sectornumber of first FAT (normally just the bootsector is reserved)
	dpb.FIRFAT_H = firstFATsector >> 8;      // offset 9 = 0x00, idem 

	if (firstFATsector != 1) {
		// todo: notice when this happens
	}

	dpb.FATCNT = sectorBuffer[16];     	     // offset 10 = 0x02, number of FATs

	byte maxEnt = 254;						
	word rootDIRentries = sectorBuffer[18]*256+sectorBuffer[17];
	if (rootDIRentries < 255) maxEnt = rootDIRentries;

	dpb.MAXENT = maxEnt;              	     // offset 11 = 0x00;  // we come up with 0xFE here, why?

	word sectorsPerFAT = sectorBuffer[23]*256+sectorBuffer[22];
	if (sectorsPerFAT > 255)
	{
		//todo: notice when this happens
	}
	word firstDIRsector = firstFATsector + (dpb.FATCNT * sectorsPerFAT);

	// the data of the disk starts at the firstDIRsector + size of the directory area
	// (the "directory" area contains max. 254 entries of 16 bytes, one entry of each file)
	word firstRecord = firstDIRsector+(maxEnt/(sectorSize/32));

	dpb.FIRREC_L = 0x21; //firstDIRsector & 0xFF;    // offset 12 = 0x21, number of first data sector
	dpb.FIRREC_H = 0; // firstDIRsector >> 8;      // offset 13 = 0x0, idem 
	
	// maxClus is the number of clusters on disk not including reserved sector, 
	// fat sectors or directory sectors, see p260 of Msx Redbook

	// bigSectors is only used for the F0 media type, it is a 32bit entry
	// for the total amount of sectors on disk
	unsigned int bigSectors = sectorBuffer[35]*256*256*256+sectorBuffer[34]*256*256+sectorBuffer[33]*256+sectorBuffer[32];
	DBERR("bigSectors: %u\n", bigSectors);		//  we come up with sectorBuffer[18] here???

	word sectorsPerCluster = sectorBuffer[13];
	word maxClus = ((bigSectors-firstRecord)/sectorsPerCluster)+1;

	dpb.MAXCLUS_L = maxClus & 0xFF;          // offset 14 = 0xF8, highest cluster number
	dpb.MAXCLUS_H = maxClus >> 8;            // offset 15 = 0x9, idem
	dpb.FATSIZ = sectorBuffer[22];           // offset 16 = 0x8, number of sectors/FAT	 

	dpb.FIRDIR_L = firstDIRsector & 0xFF;    // offset 17 = 0x11;
	dpb.FIRDIR_H = firstDIRsector >> 8;      // offset 18 = 0x00; 

	// We dont know what sectorBuffer 0x1C-1F contains on MSX harddisk images 

 	byte dpb_pre[18];
	dpb_pre[0] = 0xF0;
	dpb_pre[1] = 0x00;
	dpb_pre[2] = 0x02;
	dpb_pre[3] = 0x0F;
	dpb_pre[4] = 0x04;
	dpb_pre[5] = 0x03;
	dpb_pre[6] = 0x03;
	dpb_pre[7] = 0x01;
	dpb_pre[8] = 0x00;
	dpb_pre[9] = 0x02;
	dpb_pre[10] = 0x00;
	dpb_pre[11] = 0x21;
	dpb_pre[12] = 0x0;
	dpb_pre[13] = 0xF8;
	dpb_pre[14] = 0x9;
	dpb_pre[15] = 0x8;
	dpb_pre[16] = 0x11;
	dpb_pre[17] = 0x00;

	sendHeader();
	// send dest. address
	send(cmdData[2]);	// reg_e
	send(cmdData[3]);	// reg_d
	byte * refData = (byte *) &dpb_pre;
	byte * sendBuffer = (byte *) &dpb;

	for (int i=0;i<18;i++) {
		DBERR("GETDPB offset [%d]: 0x%02X, correct: 0x%02X\n", i+1, sendBuffer[i], refData[i]);
		send(sendBuffer[i]);
	}
}


void NowindHost::DRIVES()
{
	// at least one drive (MSXDOS1 cannot handle 0 drives)
	byte numberOfDrives = std::max<byte>(1, byte(drives.size()));

	byte reg_a = cmdData[7];
	sendHeader();
	send(getEnablePhantomDrives() ? 0x02 : 0);
	send(reg_a | (getAllowOtherDiskroms() ? 0 : 0x80));
	send(numberOfDrives);

//	romdisk = 255; // no romdisk
	for (unsigned i = 0; i < drives.size(); ++i) {
		if (drives[i]->isRomdisk()) {
			romdisk = i;
			break;
		}
	}
}

void NowindHost::INIENV()
{
	sendHeader();
	send(romdisk); // calculated in DRIVES()
}

void NowindHost::setDateMSX()
{
	time_t td = time(NULL);
	struct tm* tm = localtime(&td);

	sendHeader();
	send(tm->tm_mday);          // day
	send(tm->tm_mon + 1);       // month
	send16(tm->tm_year + 1900); // year
}

unsigned NowindHost::getSectorAmount() const
{
	byte reg_b = cmdData[1];
	return reg_b;
}

unsigned NowindHost::getStartSector() const
{
	byte reg_c = cmdData[0];
	byte reg_e = cmdData[2];
	byte reg_d = cmdData[3];
	unsigned startSector = reg_e + (reg_d * 256);

	if (reg_c < 0x80) {
		// FAT16 read/write sector
		startSector += reg_c << 16;
	}
	return startSector;
}

unsigned NowindHost::getStartAddress() const
{
	byte reg_l = cmdData[4];
	byte reg_h = cmdData[5];
	return reg_h * 256 + reg_l;
}

unsigned NowindHost::getCurrentAddress() const
{
	unsigned startAdress = getStartAddress();
	return startAdress + transferred;
}

unsigned NowindHost::getFCB() const
{
	// note: same code as getStartAddress(), merge???
	byte reg_l = cmdData[4];
	byte reg_h = cmdData[5];
	return reg_h * 256 + reg_l;
}

string NowindHost::extractName(int begin, int end) const
{
	string result;
	for (int i = begin; i < end; ++i) {
		char c = extraData[i];
		if (c == ' ') break;
		result += toupper(c);
	}
	return result;
}

int NowindHost::getDeviceNum() const
{
	unsigned fcb = getFCB();
	for (unsigned i = 0; i < MAX_DEVICES; ++i) {
		if (devices[i].fs.get() &&
		    devices[i].fcb == fcb) {
			return i;
		}
	}
	return -1;
}

int NowindHost::getFreeDeviceNum()
{
	int dev = getDeviceNum();
	if (dev != -1) {
		// There already was a device open with this fcb address,
		// reuse that device.
		return dev;
	}
	// Search for free device.
	for (unsigned i = 0; i < MAX_DEVICES; ++i) {
		if (!devices[i].fs.get()) {
			return i;
		}
	}
	// All devices are in use. This can't happen when the MSX software
	// functions correctly. We'll simply reuse the first device. It would
	// be nicer if we reuse the oldest device, but that's harder to
	// implement, and actually it doesn't really matter.
	return 0;
}

void NowindHost::deviceOpen()
{
	state = STATE_SYNC1;

	assert(recvCount == 11);
	string filename = extractName(0, 8);
	string ext      = extractName(8, 11);
	if (!ext.empty()) {
		filename += '.';
		filename += ext;
	}

	unsigned fcb = getFCB();
	unsigned dev = getFreeDeviceNum();
	devices[dev].fs.reset(new fstream()); // takes care of deleting old fs
	devices[dev].fcb = fcb;

	sendHeader();
	byte errorCode = 0;
	byte openMode = cmdData[2]; // reg_e
	switch (openMode) {
	case 1: // read-only mode
		devices[dev].fs->open(filename.c_str(), ios::in  | ios::binary);
		errorCode = 53; // file not found
		break;
	case 2: // create new file, write-only
		devices[dev].fs->open(filename.c_str(), ios::out | ios::binary);
		errorCode = 56; // bad file name
		break;
	case 8: // append to existing file, write-only
		devices[dev].fs->open(filename.c_str(), ios::out | ios::binary | ios::app);
		errorCode = 53; // file not found
		break;
	case 4:
		send(58); // sequential I/O only
		return;
	default:
		send(0xFF); // TODO figure out a good error number
		return;
	}
	assert(errorCode != 0);
	if (devices[dev].fs->fail()) {
		devices[dev].fs.reset();
		send(errorCode);
		return;
	}

	unsigned readLen = 0;
	bool eof = false;
	char buffer[256];
	if (openMode == 1) {
		// read-only mode, already buffer first 256 bytes
		readLen = readHelper1(dev, buffer);
		assert(readLen <= 256);
		eof = readLen < 256;
	}

	send(0x00); // no error
	send16(fcb);
	send16(9 + readLen + (eof ? 1 : 0)); // number of bytes to transfer

	send(openMode);
	send(0);
	send(0);
	send(0);
	send(cmdData[3]); // reg_d
	send(0);
	send(0);
	send(0);
	send(0);

	if (openMode == 1) {
		readHelper2(readLen, buffer);
	}
}

void NowindHost::deviceClose()
{
	int dev = getDeviceNum();
	if (dev == -1) return;
	devices[dev].fs.reset();
}

void NowindHost::deviceWrite()
{
	int dev = getDeviceNum();
	if (dev == -1) return;
	char data = cmdData[0]; // reg_c
	devices[dev].fs->write(&data, 1);
}

void NowindHost::deviceRead()
{
	int dev = getDeviceNum();
	if (dev == -1) return;

	char buffer[256];
	unsigned readLen = readHelper1(dev, buffer);
	bool eof = readLen < 256;
	send(0xAF);
	send(0x05);
	send(0x00); // dummy
	send16(getFCB() + 9);
	send16(readLen + (eof ? 1 : 0));
	readHelper2(readLen, buffer);
}

unsigned NowindHost::readHelper1(unsigned dev, char* buffer)
{
	assert(dev < MAX_DEVICES);
	unsigned len = 0;
	for (/**/; len < 256; ++len) {
		devices[dev].fs->read(&buffer[len], 1);
		if (devices[dev].fs->eof()) break;
	}
	return len;
}

void NowindHost::readHelper2(unsigned len, const char* buffer)
{
	for (unsigned i = 0; i < len; ++i) {
		send(buffer[i]);
	}
	if (len < 256) {
		send(0x1A); // end-of-file
	}
}

// strips a string from outer double-quotes and anything outside them
// ie: 'pre("foo")bar' will result in 'foo'
static string stripquotes(const string& str)
{
	string::size_type first = str.find_first_of('\"');
	if (first == string::npos) {
		// There are no quotes, return the whole string.
		return str;
	}
	string::size_type last  = str.find_last_of ('\"');
	if (first == last) {
		// Error, there's only a single double-quote char.
		return "";
	}
	// Return the part between the quotes.
	return str.substr(first + 1, last - first - 1);
}

void NowindHost::callImage(const string& filename)
{
	byte num = cmdData[7]; // reg_a
	if (num >= drives.size()) {
		// invalid drive number
		return;
	}
	if (drives[num]->insertDisk(stripquotes(filename))) {
		// TODO error handling
	}
}

void NowindHost::getDosVersion()
{
	sendHeader();
	send(enableMSXDOS2 ? 1:0);
}



// the MSX asks whether the host has a command  
// waiting for it to execute
void NowindHost::commandRequested()
{
    char cmdType = cmdData[1]; // reg_b
    char cmdArg = cmdData[0]; // reg_c

    switch (cmdType)
    {
    case 0x00:
        // command request at startup, read from startupRequestQueue
        commandRequestedAtStartup(cmdArg);
        break;
    case 0x01:
       commandRequestedAnytime();
       break;
    default:
        DBERR("MSX sent unknown commandRequested type %d\n", cmdType);
        break;
    }
}

// the startupRequestQueue is not cleared by the msx requesting commands
// each time the msx boots, the same startup commands are send as long 
// as the user application does not remove them
void NowindHost::commandRequestedAtStartup(byte reset)
{
    static unsigned int index = 0;
    if (reset == 0x00)
    {
        // The MSX is in its diskrom startup sequence at INIHDR and requests the first startup command
        DBERR("INIHRD hook requests command at startup\n");
        // this reset the index for startupRequestQueue
        index = 0;
    }
    else
    {
        // The MSX is in its diskrom startup sequence at INIHDR and requests the next startup command
        DBERR("INIHRD hook requests next command at startup\n");
    }

    sendHeader();

    std::vector<byte> command;
    if (index >= startupRequestQueue.size())
    {
        send(0);   // no more commands 
    }
    else
    {
        command = startupRequestQueue.at(index);
        index++;

        for (unsigned int i=0;i<command.size();i++)
        {
            send(command[i]);
        }
    }
}

// command from the requestQueue are sent only once, 
// and are them removed from the queue
void NowindHost::commandRequestedAnytime()
{
    sendHeader();
    if (requestQueue.empty())
    {
        send(0);
    }
    else
    {
        std::vector<byte> command = requestQueue.front();
        // remove command from queue        
        requestQueue.pop_front();
		if (requestQueue.empty())
		{
			send(0);
		}
		else
		{
			send(1);
		}

        for (unsigned int i=0;i<command.size();i++)
	    {
            send(command[i]);
	    }
    }
}

void NowindHost::clearStartupRequests()
{
    startupRequestQueue.clear();
}

void NowindHost::addStartupRequest(std::vector<byte> command)
{
    startupRequestQueue.push_back(command);
}


void NowindHost::clearRequests()
{
    requestQueue.clear();
}

void NowindHost::addRequest(std::vector<byte> command)
{
    requestQueue.push_back(command);
}
