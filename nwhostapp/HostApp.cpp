/**
 * @file HostApp.cpp
 *
 * @brief Contains the commandline nowind interface host application
 * @author Jan Wilmans <jw@dds.nl>  Copyright (C) 2009 Nowind
 *
 */

#include <fstream>
#include <assert.h>

// In MSC projects the getopt code must be compiled into the executable;
// therefore, the getopt sources (from glibc-2.7.tar.gz) are provided with the usbhost
// on linux the makefile doesn't use them.
// (getopt is not part of the ANSI-C standard and not included in the msvcrt)
#include <getopt.h>

#include "HostApp.h"
#include <algorithm>

// include the Nowind Host Application API
#include <libgeneral.h>
#include <libnowind.h>

#define LCase(s) std::transform(s.begin(), s.end(), s.begin(), (int(*)(int)) tolower)

using namespace std;
using namespace general;

void HostApp::initialize()
{
    nowind::initialize();
}

HostApp::HostApp() {

}

HostApp::~HostApp() {
	//dont delete mConnection here atexit() will do it.
}

void HostApp::setParameters(int argc, char *argv[])
{
	this->argc = argc;
	this->argv = argv;
}

void HostApp::debugout(const char *msg)
{
	Util::debug(msg);
}

void HostApp::updateFirmware(string sImageName) {

	int lHandle = nwa_open(1);	// open first interface found
	nwa_updatefirmware(lHandle, sImageName.c_str());
}


void HostApp::hostImage() 
{
	int lHandle = nwa_open(1);	// open first interface found
	nwa_hostimage(lHandle);
}


int HostApp::execute()
{
    Util::debug("Nowind Interface USB host application v4.0\n");

    string sImageName = "";

	// initialize the library
	nowindusb_startup();
	
	// set some defaults
	//nowindusb_attribute(attr_dos2, false);
	//nowindusb_attribute(attr_ctrl, false);
	//nowindusb_attribute(attr_shift, true);

	nowindusb_attribute(enable_dos2, false);
	nowindusb_attribute(enable_phantom_drives, true);
	nowindusb_attribute(allow_other_diskroms, false);
	

    Util::debug("Parse commandline parameters...\n");

    /* parse the commandline parameters */
    int c;
	int driveNr = 0;
     
    /* by default getopt will permute the contents of argv while scanning it so 
     * that eventually all the non-options are at the end. This allows options to 
     * be given in any order, even with programs that were not written to expect this.
     */
     
    /* option string: characters in the represent valid options, a trailing : 
     * means the option also takes na argument so:
     * "a" allows -a  
     * "a:" allows -a <somearg> 
     * "a::" means -a [<somearg>] (so the argument is optional)
     */
     
    /* in getopt.h some of the argv globals are explained (ie optind) */
    bool hasErrors = false;
    opterr = 1; 

    for (;;)
    {
        int option_index = 0;
        
/*        
 *  if needsArgument is 1, the option (--dos2 <foo>) requires the <foo> argument
 *  if storeFlag is not NULL the <foo> value is stored in *storeFlag
 *  (note: this also changes the getopt_long return value!)
 *  if the "corresponding short option" is present getopt_long will act as if
 *  the specified short option was given instead of the long option.
 */
 
/*   "optionname", needsArgument, storeFlag, 'corresponding short option'  
	needsArgument: 0= no argument, 1=requires argument, 2=optional argument
*/
        static struct option long_options[] =
        {
          {"image", 1, 0, 'i'},
          {"hdimage", 1, 0, 'm'},
          {"physical", 1, 0, 'y'},
          {"romdisk", 1, 0, 'j'},
          {"debug", 0, 0, 'd'},
          {"help", 0, 0, 'h'},
          {"rom", 1, 0, 'r'},          
          {"dos2", 0, 0, '2'},
          {"allow", 0, 0, 'a'},
          {"ctrl", 0, 0, 'c'},
          {"test", 2, 0, 't'},
          {"flash", 1, 0, 'f'},
          {"dsk2rom", 1, 0, 'z'},                    
          {"put", 1, 0, 'p'},                    
          {0, 0, 0, 0}
        };

		// : means needs argument, :: means optional-argument
		c = getopt_long (argc, argv, "i:m:jdhr:2act::f:z:p:y:", long_options, &option_index);

        // no more parameters? stop...
        if (c == -1) break;

        switch (c)
        {
        case 0: // it was a long-option
            Util::debug("option %s", long_options[option_index].name);
            if (optarg) Util::debug(" with arg %s", optarg);
            Util::debug("\n");
            break;
        
            // handle other options (short with and without arguments)
        case 'd':
			Util::debug("Install libnowind debug loginfo callback\n");
			nowindusb_set_debug_callback(&debugout);
			mDebug = true;
			break;            
        case 'i':
			// todo: check the file is valid/exists?
			Util::debug("Inserting image %s into drive %u\n", optarg, driveNr);
			nowindusb_set_image(driveNr, optarg);
			driveNr++;
			break;

/*
In Windows NT/2K/XP the CreateFile function can be used to open a disk drive or a partition on it. 
After getting a handle to the disk drive using CreateFile function the ReadFile function can be used 
to read sectors and the WriteFile function can be used to write to the drive.If you want to open 
a logical drive give the filename param of the CreateFile function as "\\\\.\\a:" or "\\\\.\\c:" ... etc. 
and if you want to open a physical drive for raw reading/writing give the filename 
param as "\\\\.\\PhysicalDrive0" or "\\\\.\\PhysicalDrive1" ... etc
*/
        case 'y':
			//todo: build a read/write through cache for drive data (physical drives can be slow!)
			Util::debug("piping physical disk into drive %u\n", driveNr);
			nowindusb_set_image(driveNr, "\\\\.\\a:");
			driveNr++;
			break;
         case 'm':
			{
				// todo: check the file is valid/exists?
				// format: file.ext:x  where x is the partition number in the file
				// format: file.ext:s-e  where s is the first partition number and e is the last
				string filespec(optarg);
				size_t pos = filespec.find(":");
				if (pos == string::npos)
				{
					// no partition specified, use auto detection 
					// - mount the boot-partition first
					// - ignore disabled partitions

					unsigned int drivesInserted = nowindusb_set_harddisk_image(driveNr, 0xff, false, optarg);
					if (drivesInserted > 0) {
						Util::debug("Inserted %u partitions using auto detection (bootable partition first)\n", drivesInserted);
						driveNr += drivesInserted;
					}
					else
					{
						Util::debug("Warning: no partitions found using auto detection!\n");
					}
				}
				else
				{
					string filename = filespec.substr(0,pos);
					string partString = filespec.substr(pos+1);
					size_t pos = partString.find("-");
					if (pos == string::npos) {
						// not :x-y syntax used, assume :x is specified.
						int part = atoi(partString.c_str());			// If the conversion cannot be performed, then atoi() will return zero
	
						// insert 
						unsigned int result = nowindusb_set_harddisk_image(driveNr, part, true, filename.c_str());
						if (result > 0) {
							Util::debug("Inserting partition %u of %s\n", part, filename.c_str());
							driveNr += result;
						} else {
							//todo:  -m c:\downloads\HDDFAT12\HDDFAT12.dsk:0 is not supported !
							Util::debug("Failed to insert partition %u of %s\n", part, filename.c_str());
						}		
					}
					else
					{
						int firstPart = atoi(partString.substr(0,pos).c_str());
						int lastPart = atoi(partString.substr(pos+1).c_str());
						if (lastPart != 0 && lastPart > firstPart)
						{
							for (int p=firstPart;p<=lastPart;p++)
							{
								// try to insert partition and respect the disabled bootflag
								unsigned int result = nowindusb_set_harddisk_image(driveNr, p, false, filename.c_str());
								if (result > 0) {
									Util::debug("Inserting partition %u of %s\n", p, filename.c_str());
									driveNr += result;
								}
								else
								{
									Util::debug("Failed to insert partition %u of %s\n", p, filename.c_str());
								}
							}					
						}

					}
				}
				break;
			}
        case 'h':
			hasErrors = true;	// actually, is not an error, help displays all options.
            break;
        case 'r':
            //nowindHost->romImage = string(optarg); 
            //nowindHost->loadRom = true;
            break;
        case '2':
			nowindusb_attribute(enable_dos2, true);
			break;
        case 'a': 
			Util::debug("Allowing other diskroms to initialize after the internal nowind diskrom!\n");
			nowindusb_attribute(allow_other_diskroms, true);
			break;
        case 'c':
			nowindusb_attribute(enable_phantom_drives, false);
			break;
        case 'f':
            sImageName = string(optarg);
            printf("Firmware update for Nowind interface: %s\n", sImageName.c_str());
            updateFirmware(sImageName);
            return 0;
        case 'z':
            diskToRom(string(optarg));
            return 0;
        case 't':
			{
			string lArgument = "read";
			if (optarg != 0) lArgument = string(optarg);
            testMode(lArgument);
            return 0;
			}
        case 'j':
            nowindusb_set_romdisk(driveNr++);        
            break;        
        case 'p':       
			nowindusb_cmd(cmd_putmem_clear, 0, 0, 0 ,0, 0);
			// filename, start-adres, mainslot, subslot, exec-adres
			nowindusb_cmd(cmd_putmem, optarg, 0x0000, 3, 2, 0x100);
            break;        
        case '?':
          hasErrors = true;
          Util::debug("hasErrors = true;\n");
          break;
          // nothing to do here (getopt gives its own error messages)
        default:
         Util::debug ("getopt returned unexpected code 0x%X ??\n", c);
        }
    }

	// are there any options left? check if they end in .dsk are
	// if they do, treat them as disk images
    if (optind < argc) {
      while (optind < argc)
      {
        string arg = string(argv[optind]);
		LCase(arg);
        if (arg.find(".dsk", 0) != string::npos) {
			nowindusb_set_image(driveNr, argv[optind]);
			driveNr++;
        } else {
            printf ("unknown option: %s\n", argv[optind]);
            hasErrors = true;
        }
        optind++;
      }
    }

    if (hasErrors) {

        printf("Usage: usbhost [options] [diskimage.dsk] [romimage.rom]\n");
        printf("Options: --image, -i    specify disk image or partition image\n");
        printf("         --hdimage, -m  specify harddisk image\n");
        printf("         --physical, -y specify physical disk\n");
        printf("         --romdisk, -j  enable romdisk\n");
        //printf("         --rom, -r      specify rom image\n");
        printf("         --flash, -f    update firmware\n");
        //printf("         --dos2, -2     boot MSXDOS 2\n");
        printf("         --ctrl, -c     only 1 drive per diskrom (same as holding CTRL)\n");
        printf("         --allow, -a    allow more diskroms to initialize\n");        
        printf("         --dsk2rom, -z  convert 360 kB image to romdisk.bin\n");
        printf("         --debug, -d    enable debug loginfo from libnowind\n");
        printf("         --test, -t[mode]  -tread (not implemented)\n");
        printf("         --test, -t[mode]  -twrite (send a fixed 'HELLO MSX' infinitely to MSX)\n");
        printf("         --test, -t[mode]  -tdev (device test, not implemented)\n");
        printf("\n");
        printf("Examples: usbhost image.dsk\n");
        printf("          usbhost kungfu.rom\n");        
        printf("          usbhost -2 harddiskimage.dsk\n");
        printf("          usbhost --flash firmware.bin\n");
		printf("          usbhost -m hdimage.dsk inserts the first partition\n");
		printf("          usbhost -m hdimage.dsk:0 inserts the first partition\n");
		printf("          usbhost -m hdimage.dsk:1-3 inserts the second, third and forth\n");
        exit(1);
    }

    hostImage();
    return 0;
}

void HostApp::processExit()
{
	if (mDebug) Util::debug("process closing down...");
	nwa_cleanup();

	//delete the object here, because the HostApp destructor will not run if ctrl-c is pressed
	delete mConnection;		
	nowindusb_cleanup();
	if (mDebug) Util::debug("done.\n");
}
