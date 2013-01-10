/*****************************************************************************
*                                                                            *
*  OpenNI 2.x Alpha                                                          *
*  Copyright (C) 2012 PrimeSense Ltd.                                        *
*                                                                            *
*  This file is part of OpenNI.                                              *
*                                                                            *
*  Licensed under the Apache License, Version 2.0 (the "License");           *
*  you may not use this file except in compliance with the License.          *
*  You may obtain a copy of the License at                                   *
*                                                                            *
*      http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                            *
*  Unless required by applicable law or agreed to in writing, software       *
*  distributed under the License is distributed on an "AS IS" BASIS,         *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
*  See the License for the specific language governing permissions and       *
*  limitations under the License.                                            *
*                                                                            *
*****************************************************************************/
#include "OniContext.h"
#include "OniStreamFrameHolder.h"
#include <XnLog.h>

static const char* ONI_CONFIGURATION_FILE = XN_FILE_LOCAL_DIR "OpenNI.ini";
#if (XN_PLATFORM == XN_PLATFORM_WIN32) && (_M_X64)
static const char* ONI_ENV_VAR_DRIVERS_REPOSITORY = "OPENNI2_DRIVERS_PATH64";
#else
static const char* ONI_ENV_VAR_DRIVERS_REPOSITORY = "OPENNI2_DRIVERS_PATH";
#endif
static const char* ONI_DEFAULT_DRIVERS_REPOSITORY = XN_FILE_LOCAL_DIR "OpenNI2" XN_FILE_DIR_SEP "Drivers";

ONI_NAMESPACE_IMPLEMENTATION_BEGIN

OniBool Context::s_valid = FALSE;

Context::Context() : m_errorLogger(xnl::ErrorLogger::GetInstance()), m_initializationCounter(0)
{
	xnOSMemSet(m_overrideDevice, 0, XN_FILE_MAX_PATH);
}

Context::~Context()
{
	s_valid = FALSE;
}

OniStatus Context::initialize()
{
	XnBool repositoryOverridden = FALSE;
	XnChar repositoryFromINI[XN_FILE_MAX_PATH] = {0};

	m_initializationCounter++;
	if (m_initializationCounter > 1)
	{
		xnLogVerbose(XN_LOG_MASK_ALL, "Initialize: Already initialized");
		return ONI_STATUS_OK;
	}

	XnStatus rc;

	rc = m_newFrameAvailableEvent.Create(FALSE);
	if (rc != XN_STATUS_OK)
	{
		m_errorLogger.Append("Couldn't create event for new frames: %s", xnGetStatusString(rc));
		return ONI_STATUS_ERROR;
	}

	s_valid = TRUE;

	// Read configuration file

	XnBool configurationFileExists = FALSE;
	rc = xnOSDoesFileExist(ONI_CONFIGURATION_FILE, &configurationFileExists);
	if (configurationFileExists)
	{
		rc = xnOSReadStringFromINI(ONI_CONFIGURATION_FILE, "Device", "Override", m_overrideDevice, XN_FILE_MAX_PATH);
		if (rc != XN_STATUS_OK)
		{
			xnLogVerbose(XN_LOG_MASK_ALL, "No override device in configuration file");
		}

		XnInt32 nValue;
		rc = xnOSReadIntFromINI(ONI_CONFIGURATION_FILE, "Log", "Verbosity", &nValue);
		if (rc == XN_STATUS_OK)
		{
			xnLogSetMaskMinSeverity(XN_LOG_MASK_ALL, (XnLogSeverity)nValue);
		}

		rc = xnOSReadIntFromINI(ONI_CONFIGURATION_FILE, "Log", "LogToConsole", &nValue);
		if (rc == XN_STATUS_OK)
		{
			xnLogSetConsoleOutput(nValue == 1);
		}
		rc = xnOSReadIntFromINI(ONI_CONFIGURATION_FILE, "Log", "LogToFile", &nValue);
		if (rc == XN_STATUS_OK)
		{
			xnLogSetFileOutput(nValue == 1);
		}
		rc = xnOSReadStringFromINI(ONI_CONFIGURATION_FILE, "Drivers", "Repository", repositoryFromINI, XN_FILE_MAX_PATH);
		if (rc == XN_STATUS_OK)
		{
			repositoryOverridden = TRUE;
		}
	}
	else
	{
		xnLogVerbose(XN_LOG_MASK_ALL, "Couldn't find configuration file '%s'", ONI_CONFIGURATION_FILE);
	}

	xnLogVerbose(XN_LOG_MASK_ALL, "OpenNI %s", ONI_VERSION_STRING);

	// Use path specified in ini file
	if (repositoryOverridden)
	{
		xnLogVerbose(XN_LOG_MASK_ALL, "Using '%s' as driver path, as configured in file '%s'", repositoryFromINI, ONI_CONFIGURATION_FILE);
		rc = loadLibraries(repositoryFromINI);
		return OniStatusFromXnStatus(rc);
	}

	xnLogVerbose(XN_LOG_MASK_ALL, "Using '%s' as driver path", ONI_DEFAULT_DRIVERS_REPOSITORY);
	// Use default path
	rc = loadLibraries(ONI_DEFAULT_DRIVERS_REPOSITORY);
	if (rc != XN_STATUS_OK)
	{
		// Can't find through default - try environment variable
		xnLogVerbose(XN_LOG_MASK_ALL, "Can't load drivers from default directory '%s'.", ONI_DEFAULT_DRIVERS_REPOSITORY);

		char dirName[XN_FILE_MAX_PATH];
		XnStatus envrc = xnOSGetEnvironmentVariable(ONI_ENV_VAR_DRIVERS_REPOSITORY, dirName, XN_FILE_MAX_PATH);
		if (envrc == XN_STATUS_OK)
		{
			xnLogVerbose(XN_LOG_MASK_ALL, "Using '%s' as driver path, as configured by environment variable '%s'", dirName, ONI_ENV_VAR_DRIVERS_REPOSITORY);
			rc = loadLibraries(dirName);
		}
	}

	if (rc == XN_STATUS_OK)
	{
		m_errorLogger.Clear();
	}

	return OniStatusFromXnStatus(rc);
}
XnStatus Context::loadLibraries(const char* directoryName)
{
	XnStatus nRetVal;
	XnChar cpSearchPath[XN_FILE_MAX_PATH] = "";
	XnChar cpSearchPattern[XN_FILE_MAX_PATH] = "";
	XnChar cpSearchString[XN_FILE_MAX_PATH] = "";

	// Build the search pattern strings
	XN_VALIDATE_STR_APPEND(cpSearchPath, directoryName, XN_FILE_MAX_PATH, nRetVal);
	XN_VALIDATE_STR_APPEND(cpSearchPath, XN_FILE_DIR_SEP, XN_FILE_MAX_PATH, nRetVal);
	XN_VALIDATE_STR_APPEND(cpSearchPattern, XN_SHARED_LIBRARY_PREFIX, XN_FILE_MAX_PATH, nRetVal);
	XN_VALIDATE_STR_APPEND(cpSearchPattern, XN_FILE_ALL_WILDCARD, XN_FILE_MAX_PATH, nRetVal);
	XN_VALIDATE_STR_APPEND(cpSearchPattern, XN_SHARED_LIBRARY_POSTFIX, XN_FILE_MAX_PATH, nRetVal);
	XN_VALIDATE_STR_APPEND(cpSearchString, cpSearchPath, XN_FILE_MAX_PATH, nRetVal);
	XN_VALIDATE_STR_APPEND(cpSearchString, cpSearchPattern, XN_FILE_MAX_PATH, nRetVal);	

	// Get a file list of Xiron devices
	XnInt32 nFileCount = 0;
	nRetVal = xnOSCountFiles(cpSearchString, &nFileCount);
	if (nRetVal != XN_STATUS_OK || nFileCount == 0)
	{
		xnLogError(XN_LOG_MASK_ALL, "Found no drivers matching '%s'", cpSearchString);
		m_errorLogger.Append("Found no files matching '%s'", cpSearchString);
		return XN_STATUS_NO_MODULES_FOUND;
	}
	
	// Save directory
	XnChar workingDir[XN_FILE_MAX_PATH];
	xnOSGetCurrentDir(workingDir, XN_FILE_MAX_PATH);
	// Change directory
	xnOSSetCurrentDir(cpSearchPath);

	typedef XnChar FileName[XN_FILE_MAX_PATH];
	FileName* acsFileList = XN_NEW_ARR(FileName, nFileCount);
	nRetVal = xnOSGetFileList(cpSearchPattern, cpSearchPath, acsFileList, nFileCount, &nFileCount);

	// Return to directory
	xnOSSetCurrentDir(workingDir);

	for (int i = 0; i < nFileCount; ++i)
	{
		DeviceDriver* pDeviceDriver = XN_NEW(DeviceDriver, acsFileList[i], m_errorLogger);
		if (pDeviceDriver == NULL || !pDeviceDriver->isValid())
		{
			xnLogVerbose(XN_LOG_MASK_ALL, "Couldn't use file '%s' as a device driver", acsFileList[i]);
			m_errorLogger.Append("Couldn't understand file '%s' as a device driver", acsFileList[i]);
			XN_DELETE(pDeviceDriver);
			continue;
		}
		OniCallbackHandle dummy;
		pDeviceDriver->registerDeviceConnectedCallback(deviceDriver_DeviceConnected, this, dummy);
		pDeviceDriver->registerDeviceDisconnectedCallback(deviceDriver_DeviceDisconnected, this, dummy);
		pDeviceDriver->registerDeviceStateChangedCallback(deviceDriver_DeviceStateChanged, this, dummy);
		if (!pDeviceDriver->initialize())
		{
			xnLogVerbose(XN_LOG_MASK_ALL, "Couldn't use file '%s' as a device driver", acsFileList[i]);
			m_errorLogger.Append("Couldn't initialize device driver from file '%s'", acsFileList[i]);
			XN_DELETE(pDeviceDriver);
			continue;
		}
		m_cs.Lock();
		m_deviceDrivers.AddLast(pDeviceDriver);
		m_cs.Unlock();
	}

	if (m_deviceDrivers.Size() == 0)
	{
		xnLogError(XN_LOG_MASK_ALL, "Found no valid drivers");
		m_errorLogger.Append("Found no valid drivers in '%s'", directoryName);
		return XN_STATUS_NO_MODULES_FOUND;
	}

	XN_DELETE_ARR(acsFileList);

	return XN_STATUS_OK;
}
void Context::shutdown()
{
	--m_initializationCounter;
	if (m_initializationCounter > 0)
	{
		xnLogInfo(XN_LOG_MASK_ALL, "Shutdown: still need %d more shutdown calls (to match initializations)", m_initializationCounter);
		return;
	}
	if (!s_valid)
	{
		return;
	}

	s_valid = FALSE;

	m_cs.Lock();

    // Close all recorders.
    while (m_recorders.Begin() != m_recorders.End())
    {
        Recorder* pRecorder = *m_recorders.Begin();
        recorderClose(pRecorder);
    }

	// Destroy all streams
	while (m_streams.Begin() != m_streams.End())
	{
		VideoStream* pStream = *m_streams.Begin();
		streamDestroy(pStream);
	}

	// Close all devices
	while (m_devices.Begin() != m_devices.End())
	{
		Device* pDevice = *m_devices.Begin();
		m_devices.Remove(pDevice);
		pDevice->close();
		XN_DELETE(pDevice);
	}

	for (xnl::List<DeviceDriver*>::Iterator iter = m_deviceDrivers.Begin(); iter != m_deviceDrivers.End(); ++iter)
	{
		DeviceDriver* pDriver = *iter;
		XN_DELETE(pDriver);
	}
	m_deviceDrivers.Clear();

	m_newFrameAvailableEvent.Close();

	m_cs.Unlock();
}

OniStatus Context::registerDeviceConnectedCallback(OniDeviceInfoCallback handler, void* pCookie, OniCallbackHandle& handle)
{
	return OniStatusFromXnStatus(m_deviceConnectedEvent.Register(handler, pCookie, (XnCallbackHandle&)handle));
}
void Context::unregisterDeviceConnectedCallback(OniCallbackHandle handle)
{
	m_deviceConnectedEvent.Unregister((XnCallbackHandle)handle);
}
OniStatus Context::registerDeviceDisconnectedCallback(OniDeviceInfoCallback handler, void* pCookie, OniCallbackHandle& handle)
{
	return OniStatusFromXnStatus(m_deviceDisconnectedEvent.Register(handler, pCookie, (XnCallbackHandle&)handle));
}
void Context::unregisterDeviceDisconnectedCallback(OniCallbackHandle handle)
{
	m_deviceDisconnectedEvent.Unregister((XnCallbackHandle)handle);
}
OniStatus Context::registerDeviceStateChangedCallback(OniDeviceStateCallback handler, void* pCookie, OniCallbackHandle& handle)
{
	return OniStatusFromXnStatus(m_deviceStateChangedEvent.Register(handler, pCookie, (XnCallbackHandle&)handle));
}
void Context::unregisterDeviceStateChangedCallback(OniCallbackHandle handle)
{
	m_deviceStateChangedEvent.Unregister((XnCallbackHandle)handle);
}

OniStatus Context::getDeviceList(OniDeviceInfo** pDevices, int* pDeviceCount)
{
	m_cs.Lock();

	*pDeviceCount = m_devices.Size();
	*pDevices = XN_NEW_ARR(OniDeviceInfo, *pDeviceCount);

	int idx = 0;
	for (xnl::List<Device*>::ConstIterator iter = m_devices.Begin(); iter != m_devices.End(); ++iter, ++idx)
	{
		xnOSMemCopy((*pDevices)+idx, (*iter)->getInfo(), sizeof(OniDeviceInfo));
	}

	m_cs.Unlock();
	return ONI_STATUS_OK;

}
OniStatus Context::releaseDeviceList(OniDeviceInfo* pDevices)
{
	XN_DELETE_ARR(pDevices);
	return ONI_STATUS_OK;
}

OniStatus Context::deviceOpen(const char* uri, OniDeviceHandle* pDevice)
{
	oni::implementation::Device* pMyDevice = NULL;

	const char* deviceURI = uri;
	if (xnOSStrLen(m_overrideDevice) > 0)
		deviceURI = m_overrideDevice;

	xnLogVerbose(XN_LOG_MASK_ALL, "Trying to open device by URI '%s'", deviceURI == NULL ? "(NULL)" : deviceURI);

	m_cs.Lock();

	if (deviceURI == NULL)
	{
		// Default
		if (m_devices.Size() == 0)
		{
			m_errorLogger.Append("DeviceOpen using default: no devices found");
			xnLogError(XN_LOG_MASK_ALL, "Can't open default device - none found");
			m_cs.Unlock();
			return ONI_STATUS_ERROR;
		}

		pMyDevice = *m_devices.Begin();
	}
	else
	{
		for (xnl::List<Device*>::Iterator iter = m_devices.Begin(); iter != m_devices.End(); ++iter)
		{
			if (xnOSStrCmp((*iter)->getInfo()->uri, deviceURI) == 0)
			{
				pMyDevice = *iter;
			}
		}
	}

	if (pMyDevice == NULL)
	{
		for (xnl::List<DeviceDriver*>::Iterator iter = m_deviceDrivers.Begin(); iter != m_deviceDrivers.End() && pMyDevice == NULL; ++iter)
		{
			if ((*iter)->tryDevice(deviceURI))
			{
				for (xnl::List<Device*>::Iterator iter = m_devices.Begin(); iter != m_devices.End(); ++iter)
				{
					if (xnOSStrCmp((*iter)->getInfo()->uri, deviceURI) == 0)
					{
						pMyDevice = *iter;
						break;
					}
				}
			}
			else
			{
//					printf("Not yet\n");
			}
		}
	}

	m_cs.Unlock();

	if (pMyDevice == NULL)
	{
		xnLogError("Couldn't open device '%s'", uri);
		m_errorLogger.Append("DeviceOpen: Couldn't open device '%s'", uri);
		return ONI_STATUS_ERROR;
	}

	_OniDevice* pDeviceHandle = XN_NEW(_OniDevice);
	if (pDeviceHandle == NULL)
	{
		m_errorLogger.Append("Couldn't allocate memory for DeviceHandle");
		return ONI_STATUS_ERROR;
	}
	*pDevice = pDeviceHandle;
	pDeviceHandle->pDevice = pMyDevice;
	m_deviceToHandle[pMyDevice] = pDeviceHandle;

	return pMyDevice->open();
}

OniStatus Context::deviceClose(OniDeviceHandle device)
{
	if (device == NULL)
	{
		return ONI_STATUS_ERROR;
	}
	Device* pDevice = device->pDevice;

	pDevice->close();

	XN_DELETE(device);
	return ONI_STATUS_OK;
}

const OniSensorInfo* Context::getSensorInfo(OniDeviceHandle device, OniSensorType sensorType)
{
	Device* pDevice = device->pDevice;

	OniSensorInfo *pSensorInfos;
	int sensors = ONI_MAX_SENSORS;
	pDevice->getSensorInfoList(&pSensorInfos, sensors);

	for (int i = 0; i < sensors; ++i)
	{
		if (pSensorInfos[i].sensorType == sensorType)
		{
			return (&pSensorInfos[i]);
		}
	}

	return NULL;
}

const OniSensorInfo* Context::getSensorInfo(OniStreamHandle stream)
{
	if (stream == NULL || stream->pStream == NULL)
	{
		m_errorLogger.Append("Invalid stream");
		return NULL;
	}

	return stream->pStream->getSensorInfo();
}

OniStatus Context::createStream(OniDeviceHandle device, OniSensorType sensorType, OniStreamHandle* pStream)
{

	// Create the stream.
	Device* pDevice = device->pDevice;
	VideoStream* pMyStream = pDevice->createStream(sensorType);
	if (pMyStream == NULL)
	{
		m_errorLogger.Append("Context: Couldn't create stream from device:%08x, source: %d", device, sensorType);
		return ONI_STATUS_ERROR;
	}

	pMyStream->setContextNewFrameEvent(&m_newFrameAvailableEvent);
	// Create stream frame holder and connect it to the stream.
	StreamFrameHolder* pFrameHolder = XN_NEW(StreamFrameHolder, pMyStream);
	if (pFrameHolder == NULL)
	{
		m_errorLogger.Append("Context: Couldn't create stream frame holder from device:%08x, source: %d", device, sensorType);
		XN_DELETE(pMyStream);
		return ONI_STATUS_ERROR;
	}
	pMyStream->setFrameHolder(pFrameHolder);

	// Create handle object.
	_OniStream* pStreamHandle = XN_NEW(_OniStream);
	if (pStreamHandle == NULL)
	{
		m_errorLogger.Append("Couldn't allocate memory for StreamHandle");
		XN_DELETE(pFrameHolder);
		pFrameHolder = NULL;
		XN_DELETE(pMyStream);
		pMyStream = NULL;
		return ONI_STATUS_ERROR;
	}
	*pStream = pStreamHandle;
	pStreamHandle->pStream = pMyStream;

	m_cs.Lock();
	m_streams.AddLast(pMyStream);
	m_cs.Unlock();

	return ONI_STATUS_OK;
}

OniStatus Context::streamDestroy(OniStreamHandle stream)
{
	OniStatus rc = ONI_STATUS_OK;

	if (stream == NULL)
	{
		return ONI_STATUS_OK;
	}

	VideoStream* pStream = stream->pStream;
	rc = streamDestroy(pStream);
	if (rc == ONI_STATUS_OK)
	{
		XN_DELETE(stream);
	}
	return rc;
}

OniStatus Context::streamDestroy(VideoStream* pStream)
{
	OniStatus rc = ONI_STATUS_OK;

	if (pStream == NULL)
	{
		return ONI_STATUS_OK;
	}

	// Make sure the stream is stopped.
	pStream->stop();

	m_cs.Lock();

	// Remove the stream from the streams list.
	m_streams.Remove(pStream);

	m_cs.Unlock();

	// Lock stream's frame holder.
	FrameHolder* pFrameHolder = pStream->getFrameHolder();
	pFrameHolder->setEnabled(FALSE);
	pFrameHolder->lock();
	pFrameHolder->clear();

	// Get the frame holder's streams.
	int numStreams = pFrameHolder->getNumStreams();
	xnl::Array<VideoStream*> pStreamList(numStreams);
	pStreamList.SetSize(numStreams);
	pFrameHolder->getStreams(pStreamList.GetData(), &numStreams);

	// Change holder to all the streams (allocate new StreamFrameHolder).
	for (int i = 0; i < numStreams; ++i)
	{
		if (pStreamList[i] != pStream)
		{
			// Allocate new frame holder.
			StreamFrameHolder* pStreamFrameHolder = XN_NEW(StreamFrameHolder, pStreamList[i]);
			if (pStreamFrameHolder == NULL)
			{
				rc = ONI_STATUS_ERROR;
				continue;
			}

			// Replace the holder in the stream.
			pStreamList[i]->setFrameHolder(pStreamFrameHolder);
		}
	}

	pFrameHolder->unlock();

	// Delete the stream object and handle.
	XN_DELETE(pStream);

	// Delete the frame holder.
	XN_DELETE(pFrameHolder);

	return rc;
}

OniStatus Context::readFrame(OniStreamHandle stream, OniFrame** pFrame)
{
	// Make sure frame is available.
	int streamIndex;
	OniStatus rc = waitForStreams(&stream, 1, &streamIndex, ONI_TIMEOUT_FOREVER);
	if (rc != ONI_STATUS_OK)
	{
		return rc;
	}

	// Get the actual frame.
	_OniStream* pStream = (_OniStream*)stream;
	return pStream->pStream->readFrame(pFrame);
}

void Context::frameRelease(OniFrame* pFrame)
{
	oni::implementation::VideoStream* pStream = oni::implementation::VideoStream::getFrameStream(pFrame);

	if (m_streams.Find(pStream) != m_streams.End())
	{
		pStream->frameRelease(pFrame);
	}
}

void Context::frameAddRef(OniFrame* pFrame)
{
	oni::implementation::VideoStream* pStream = oni::implementation::VideoStream::getFrameStream(pFrame);
	pStream->frameAddRef(pFrame);
}

OniStatus Context::waitForStreams(OniStreamHandle* pStreams, int streamCount, int* pStreamIndex, int timeout)
{
	static const int MAX_WAITED_DEVICES = 20;
	Device* deviceList[MAX_WAITED_DEVICES];

	unsigned long long oldestTimestamp = XN_MAX_UINT64;
	int oldestIndex = -1;

	int numDevices = 0;
	for (int i = 0; i < streamCount; ++i)
	{
		if (pStreams[i] != NULL)
		{
			VideoStream* pStream = ((_OniStream*)pStreams[i])->pStream;
			Device* pDevice = &pStream->getDevice();

			// Check if device already exists.
			bool found = false;
			for (int j = 0; j < numDevices; ++j)
			{
				if (deviceList[j] == pDevice)
				{
					found = true;
					break;
				}
			}

			// Add new device to list.
			if (!found)
			{
				if (numDevices < MAX_WAITED_DEVICES)
				{
					deviceList[numDevices] = pDevice;
					++numDevices;
				}
				else
				{
					// Cannot wait on streams from more than MAX_WAITED_DEVICES devices.
					return ONI_STATUS_NOT_SUPPORTED;
				}
			}
		}
	}

	do
	{
		for (int i = 0; i < streamCount; ++i)
		{
			if (pStreams[i] == NULL)
				continue;

			VideoStream* pStream = ((_OniStream*)pStreams[i])->pStream;
			pStream->lockFrame();
			OniFrame* pFrame = pStream->peekFrame();
			if (pFrame != NULL && pFrame->timestamp < oldestTimestamp)
			{
				oldestTimestamp = pFrame->timestamp;
				oldestIndex = i;
			}
			pStream->unlockFrame();
		}

		if (oldestIndex != -1)
		{
			*pStreamIndex = oldestIndex;
			return ONI_STATUS_OK;
		}

		// 'Poke' the driver to attempt to receive more frames.
		for (int j = 0; j < numDevices; ++j)
		{
			deviceList[j]->tryManualTrigger();
		}

	} while (m_newFrameAvailableEvent.Wait(timeout) == XN_STATUS_OK);

	m_errorLogger.Append("waitForStreams: timeout reached");
	return ONI_STATUS_TIME_OUT;
}

OniStatus Context::enableFrameSync(OniStreamHandle* pStreams, int numStreams, OniFrameSyncHandle* pFrameSyncHandle)
{
	// Verify parameters.
	if (pFrameSyncHandle == NULL)
	{
		return ONI_STATUS_BAD_PARAMETER;
	}

	xnl::Array<VideoStream*> pStreamList(numStreams);
	DeviceDriver* pDeviceDriver = NULL;

	// Set the size of the arrays, so they can be filled.
	pStreamList.SetSize(numStreams);

	// Check validity and fill the arrays.
	for (int i = 0; i < numStreams; ++i)
	{
		// Make sure stream's device is valid and is same as device of other streams. 
		if (pDeviceDriver == NULL)
		{
			pDeviceDriver = pStreams[i]->pStream->getDevice().getDeviceDriver();
		}
		else
		{
			// Check whether device is different than previous devices.
			if (pDeviceDriver != pStreams[i]->pStream->getDevice().getDeviceDriver())
			{
				// Frame sync groups using streams from different drivers is not supported.
				m_errorLogger.Append("EnableFrameSync: can't sync streams from different drivers");
				return ONI_STATUS_NOT_SUPPORTED;
			}
		}

		// Make sure stream does not already belong to stream group.
		/*if (pStreams[i]->pStream->GetFrameSyncGroup() != NULL)
		{
			// TODO: add ONI_STATUS_ALREADY_EXISTS?
			return ONI_STATUS_ERROR;
		}*/

		// Store the stream pointer.
		pStreamList[i] = pStreams[i]->pStream;
	}

	return enableFrameSyncEx(pStreamList.GetData(), numStreams, pDeviceDriver, pFrameSyncHandle);
}

OniStatus Context::enableFrameSyncEx(VideoStream** pStreams, int numStreams, DeviceDriver* pDeviceDriver, OniFrameSyncHandle* pFrameSyncHandle)
{
	// Make sure the device driver is valid.
	if (pDeviceDriver == NULL)
	{
		return ONI_STATUS_ERROR;
	}

	// Create the new frame sync group (it will link all the streams).
	SyncedStreamsFrameHolder* pSyncedStreamsFrameHolder = XN_NEW(SyncedStreamsFrameHolder, 
																	pStreams, numStreams);
	XN_VALIDATE_PTR(pSyncedStreamsFrameHolder, ONI_STATUS_ERROR);

	// Configure frame-sync group in driver.
	void* driverHandle = pDeviceDriver->enableFrameSync(pStreams, numStreams);
	XN_VALIDATE_PTR(driverHandle, ONI_STATUS_ERROR);

	// Return the frame sync handle.
	*pFrameSyncHandle = XN_NEW(_OniFrameSync);
	if (*pFrameSyncHandle == NULL)
	{
		m_errorLogger.Append("Couldn't allocate memory for FrameSyncHandle");
		return ONI_STATUS_ERROR;
	}
	(*pFrameSyncHandle)->pSyncedStreamsFrameHolder = pSyncedStreamsFrameHolder;
	(*pFrameSyncHandle)->pDeviceDriver = pDeviceDriver;
	(*pFrameSyncHandle)->pFrameSyncHandle = driverHandle;

	// Update the frame holders of all the streams.
	pSyncedStreamsFrameHolder->lock();
	for (int j = 0; j < numStreams; ++j)
	{
		FrameHolder* pOldFrameHolder = pStreams[j]->getFrameHolder();
		pOldFrameHolder->lock();
		pOldFrameHolder->setStreamEnabled(pStreams[j], FALSE);
		pStreams[j]->setFrameHolder(pSyncedStreamsFrameHolder);
		pOldFrameHolder->unlock();
		XN_DELETE(pOldFrameHolder);
	}
	pSyncedStreamsFrameHolder->unlock();

	return ONI_STATUS_OK;

}

void Context::disableFrameSync(OniFrameSyncHandle frameSyncHandle)
{
	if (frameSyncHandle == NULL)
	{
		m_errorLogger.Append("Disable Frame Sync: Invalid handle");
		return;
	}

	// Disable the frame sync in the driver.
	frameSyncHandle->pDeviceDriver->disableFrameSync(frameSyncHandle->pFrameSyncHandle);

	// Disable and clear the synced stream frame holder.
	frameSyncHandle->pSyncedStreamsFrameHolder->setEnabled(FALSE);
	frameSyncHandle->pSyncedStreamsFrameHolder->lock();
	frameSyncHandle->pSyncedStreamsFrameHolder->clear();

	// Get the stream list from the holder.
	int numStreams = frameSyncHandle->pSyncedStreamsFrameHolder->getNumStreams();
	xnl::Array<VideoStream*> pStreamList(numStreams);
	pStreamList.SetSize(numStreams);
	frameSyncHandle->pSyncedStreamsFrameHolder->getStreams(pStreamList.GetData(), &numStreams);

	// Change holder to all the streams (allocate new StreamFrameHolder).
	for (int i = 0; i < numStreams; ++i)
	{
		// Allocate new frame holder.
		StreamFrameHolder* pStreamFrameHolder = XN_NEW(StreamFrameHolder, pStreamList[i]);
		if (pStreamFrameHolder == NULL)
		{
			// TODO: error!!!
			continue;
		}

		// Replace the holder in the stream.
		pStreamList[i]->setFrameHolder(pStreamFrameHolder);
	}
	frameSyncHandle->pSyncedStreamsFrameHolder->unlock();

	// Delete the frame sync group (it will remove the link from all the streams).
	XN_DELETE(frameSyncHandle->pSyncedStreamsFrameHolder);
	XN_DELETE(frameSyncHandle);
}

const char* Context::getExtendedError()
{
	return m_errorLogger.GetExtendedError();
}

void ONI_CALLBACK_TYPE Context::deviceDriver_DeviceConnected(Device* pDevice, void* pCookie)
{
	Context* pContext = (Context*)pCookie;

	pContext->m_cs.Lock();
	pContext->m_devices.AddLast(pDevice);
	pContext->m_cs.Unlock();

	pContext->m_deviceConnectedEvent.Raise(pDevice->getInfo());
}
void ONI_CALLBACK_TYPE Context::deviceDriver_DeviceDisconnected(Device* pDevice, void* pCookie)
{
	Context* pContext = (Context*)pCookie;

	pContext->m_cs.Lock();
	pContext->m_devices.Remove(pDevice);
	pContext->m_cs.Unlock();

	pContext->m_deviceDisconnectedEvent.Raise(pDevice->getInfo());
}
void ONI_CALLBACK_TYPE Context::deviceDriver_DeviceStateChanged(Device* pDevice, OniDeviceState deviceState, void* pCookie)
{
	Context* pContext = (Context*)pCookie;
	pContext->m_deviceStateChangedEvent.Raise(pDevice->getInfo(), deviceState);
}

OniStatus Context::recorderOpen(const char* fileName, OniRecorderHandle* pRecorder)
{
    // Validate parameters.
    if (NULL == pRecorder || NULL == fileName)
    {
        return ONI_STATUS_BAD_PARAMETER;
    }
    // Allocate the handle.
    *pRecorder = XN_NEW(_OniRecorder);
    if (NULL == *pRecorder)
    {
        return ONI_STATUS_ERROR;
    }
    // Create the recorder itself.
    if (NULL == ((*pRecorder)->pRecorder = XN_NEW(Recorder, m_errorLogger, *pRecorder)))
    {
        XN_DELETE(*pRecorder);
        return ONI_STATUS_ERROR;
    }
    // Try to initialize the recorder, and add it to the list of known
    // recorders upon successful initialization.
    OniStatus status = (*pRecorder)->pRecorder->initialize(fileName);
    if (ONI_STATUS_OK == status) 
    {
        m_recorders.AddLast((*pRecorder)->pRecorder);
    }
    else
    {
        XN_DELETE((*pRecorder)->pRecorder);
    }
    return status;
}
OniStatus Context::recorderClose(OniRecorderHandle* pRecorder)
{
    // Validate parameters.
    if (NULL == pRecorder)
    {
        return ONI_STATUS_BAD_PARAMETER;
    }

    // NOTE:
    //  The way handles are related to Recorder instance can be depicted by such
    //  a diagram:
    //
    //  +----------------------------+ points to 
    //  | OniRecorderHandle handle_1 |-----------------+
    //  +----------------------------+                 |
    //  +----------------------------+ points to +-----v------------------+
    //  | OniRecorderHandle handle_2 |---------->| _OniRecorder instance  |
    //  +----------------------------+           |------------------------|
    //                                           | Recorder* pRecorder    |
    //  +-------------------+          points to +-----|------------------+
    //  | Recorder instance |<-------------------------+
    //  +-------------------+
    //
    // As you see, there might be two instances of OniRecorderHandle, which point
    // to the same Recorder instance.
    //
    // Handles do not support any reference-counting, and thus whenever somebody
    // destroys a Recorder instance, the instance becomes nonexistent for every
    // handle out there in your program.
    //
    // Moreover, a Recorder instance might own a handle to itself, and whenever
    // the Recorder instance is being destroyed, it NULL-fies the pRecorder
    // field in _OniRecorder structure.
    if (NULL != *pRecorder)
    {
        recorderClose((*pRecorder)->pRecorder);
    }

    // Delete the _OniRecorder data structure.
    XN_DELETE(*pRecorder);

    // Ensure, that the client no longer considers the handle being a valid one.
    *pRecorder = NULL;

    return ONI_STATUS_OK;
}
OniStatus Context::recorderClose(Recorder* pRecorder)
{
    // Validate parameters.
    if (NULL == pRecorder)
    {
        return ONI_STATUS_BAD_PARAMETER;
    }
    pRecorder->stop();
    pRecorder->detachAllStreams();
    m_recorders.Remove(pRecorder);
    XN_DELETE(pRecorder);
    return ONI_STATUS_OK;
}

void Context::clearErrorLogger()
{
	m_errorLogger.Clear();
}

ONI_NAMESPACE_IMPLEMENTATION_END
