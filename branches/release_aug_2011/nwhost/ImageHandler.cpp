// ImageHandler.cpp
// an image handler is like a diskdrive, it contains a SectorMedium (a disk) 
// it mostly just passed information from/to/about the disk it contains

#include <stdio.h>
#include <cassert>

#include "ImageHandler.h"

using namespace std;

SectorMedium * ImageHandler::getSectorMedium() {
	return &image;
}

int ImageHandler::insertDisk(std::string filename) {
    if (filename == strRomdisk)
    {
        image.setRomdisk();
        return 0; // always successful
    }
	return image.openDiskImage(filename) ? 0 : -1;
}

bool ImageHandler::diskChanged() {
	return image.isDiskChanged();
}

bool ImageHandler::isRomdisk() {
	return image.isRomdisk();
}

std::string ImageHandler::getDescription() {
	return image.getDescription();
}
