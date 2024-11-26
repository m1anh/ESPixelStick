/*
* InputFPPRemote.cpp
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2021, 2022 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*/

#include "../ESPixelStick.h"
#include <Int64String.h>
#include "InputFPPRemote.h"
#include "../service/FPPDiscovery.h"
#include "InputFPPRemotePlayFile.hpp"
#include "InputFPPRemotePlayList.hpp"

#if defined ARDUINO_ARCH_ESP32
#   include <functional>

static TaskHandle_t PollTaskHandle = NULL;
static c_InputFPPRemote *pFppRemoteInstance = nullptr;
//----------------------------------------------------------------------------
void FppRemoteTask (void *arg)
{
    uint32_t PollStartTime = millis();
    uint32_t PollEndTime = PollStartTime;
    uint32_t PollTime = pdMS_TO_TICKS(25);
    const uint32_t MinPollTimeMs = 25;

    while(1)
    {
        uint32_t DeltaTime = PollEndTime - PollStartTime;

        if (DeltaTime < MinPollTimeMs)
        {
            PollTime = pdMS_TO_TICKS(MinPollTimeMs - DeltaTime);
        }
        else
        {
            // handle time wrap and long frames
            PollTime = pdMS_TO_TICKS(1);
        }
        vTaskDelay(PollTime);

        PollStartTime = millis();

        pFppRemoteInstance->TaskProcess();

        // record the loop end time
        PollEndTime = millis();
    }
} // FppRemoteTask
#endif // def ARDUINO_ARCH_ESP32

//-----------------------------------------------------------------------------
c_InputFPPRemote::c_InputFPPRemote (c_InputMgr::e_InputChannelIds NewInputChannelId,
                                    c_InputMgr::e_InputType       NewChannelType,
                                    uint32_t                        BufferSize) :
    c_InputCommon (NewInputChannelId, NewChannelType, BufferSize)
{
    // DEBUG_START;

//     WebMgr.RegisterFPPRemoteCallback ([this](EspFPPRemoteDevice* pDevice) {this->onMessage (pDevice); });

    // DEBUG_END;
} // c_InputE131

//-----------------------------------------------------------------------------
c_InputFPPRemote::~c_InputFPPRemote ()
{
    if (HasBeenInitialized)
    {
        StopPlaying ();
    }

#ifdef ARDUINO_ARCH_ESP32
    if(PollTaskHandle)
    {
        logcon("Stop FPP Remote Task");
        vTaskDelete(PollTaskHandle);
        PollTaskHandle = NULL;
    }
#endif // def ARDUINO_ARCH_ESP32

} // ~c_InputFPPRemote

//-----------------------------------------------------------------------------
void c_InputFPPRemote::Begin ()
{
    // DEBUG_START;

    HasBeenInitialized = true;

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_InputFPPRemote::GetConfig (JsonObject& jsonConfig)
{
    // DEBUG_START;

    if (PlayingFile ())
    {
        jsonConfig[JSON_NAME_FILE_TO_PLAY] = pInputFPPRemotePlayItem->GetFileName ();
    }
    else
    {
        jsonConfig[JSON_NAME_FILE_TO_PLAY] = No_LocalFileToPlay;
    }
    jsonConfig[CN_SyncOffset]  = SyncOffsetMS;
    jsonConfig[CN_SendFppSync] = SendFppSync;
    jsonConfig[CN_blankOnStop] = FPPDiscovery.GetBlankOnStop();

    // DEBUG_END;

} // GetConfig

//-----------------------------------------------------------------------------
void c_InputFPPRemote::GetStatus (JsonObject& jsonStatus)
{
    // DEBUG_START;

    JsonObject LocalPlayerStatus = jsonStatus[F ("Player")].to<JsonObject> ();
    LocalPlayerStatus[CN_id] = InputChannelId;
    LocalPlayerStatus[CN_active] = PlayingFile ();

    if (PlayingRemoteFile ())
    {
        FPPDiscovery.GetStatus (LocalPlayerStatus);
    }

    else if (PlayingFile ())
    {
        JsonObject PlayerObjectStatus = LocalPlayerStatus[StatusType].to<JsonObject> ();
        pInputFPPRemotePlayItem->GetStatus (PlayerObjectStatus);
    }

    else
    {
        // DEBUG_V ("Not Playing");
    }

    // DEBUG_END;

} // GetStatus

//-----------------------------------------------------------------------------
void c_InputFPPRemote::PlayNextFile ()
{
    // DEBUG_START;
    do // once
    {
        std::vector<String> ListOfFiles;
        FileMgr.GetListOfSdFiles(ListOfFiles);
        ListOfFiles.shrink_to_fit();
        // DEBUG_V(String("Num Files Found: ") + String(ListOfFiles.size()));

        if(0 == ListOfFiles.size())
        {
            // no files. Dont continue
            break;
        }

        // DEBUG_V(String("File Being Played: '") + FileBeingPlayed + "'");
        // find the current file
        std::vector<String>::iterator FileIterator = ListOfFiles.end();
        std::vector<String>::iterator CurrentFileInList = ListOfFiles.begin();
        while (CurrentFileInList != ListOfFiles.end())
        {
            // DEBUG_V(String("CurrentFileInList: '") + *CurrentFileInList + "'");

            if((*CurrentFileInList).equals(FileBeingPlayed))
            {
                // DEBUG_V("Found File");
                FileIterator = ++CurrentFileInList;
                break;
            }
            ++CurrentFileInList;
        }

        // DEBUG_V("now find the next file");
        do
        {
            // did we wrap?
            if(ListOfFiles.end() == CurrentFileInList)
            {
                // DEBUG_V("Handle wrap");
                CurrentFileInList = ListOfFiles.begin();
            }
            // DEBUG_V(String("CurrentFileInList: '") + *CurrentFileInList + "'");

            // is this a valid file?
            if( (-1 != (*CurrentFileInList).indexOf(".fseq")) && (-1 != (*CurrentFileInList).indexOf(".pl")))
            {
                // DEBUG_V("try the next file");
                // get the next file
                ++CurrentFileInList;

                continue;
            }

            // DEBUG_V(String("CurrentFileInList: '") + *CurrentFileInList + "'");

            // did we come all the way around?
            if((*CurrentFileInList).equals(FileBeingPlayed))
            {
                // DEBUG_V("no other file to play. Keep playing it.");
                break;
            }

            // DEBUG_V(String("Succes - we have found a new file to play: '") + *CurrentFileInList + "'");
            StopPlaying();
            StartPlaying(*CurrentFileInList);
            break;
        } while (FileIterator != CurrentFileInList);

    } while(false);

    // DEBUG_END;

} // PlayNextFile

//-----------------------------------------------------------------------------
void c_InputFPPRemote::Process ()
{
    // DEBUG_START;
#ifndef ARDUINO_ARCH_ESP32
    TaskProcess();
#else
    if(!PollTaskHandle)
    {
        logcon("Start FPP Remote Task");
        pFppRemoteInstance = this;
        xTaskCreatePinnedToCore(FppRemoteTask, "FppRemoteTask", 4096, NULL, FPP_REMOTE_TASK_PRIORITY, &PollTaskHandle, 1);
        vTaskPrioritySet(PollTaskHandle, FPP_REMOTE_TASK_PRIORITY);
        // DEBUG_V("End FppRemoteTask");
    }
#endif // ndef ARDUINO_ARCH_ESP32
    // DEBUG_END;

} // process

//-----------------------------------------------------------------------------
void c_InputFPPRemote::TaskProcess ()
{
    // DEBUG_START;
    if (!IsInputChannelActive || StayDark)
    {
        // DEBUG_V ("dont do anything if the channel is not active");
        StopPlaying ();
    }
    else if (PlayingRemoteFile ())
    {
        // DEBUG_V ("Remote File Play");
        while(Poll ()) {}
    }
    else if (PlayingFile ())
    {
        // DEBUG_V ("Local File Play");
        while(Poll ()) {}

        if (pInputFPPRemotePlayItem->IsIdle ())
        {
            // DEBUG_V ("Idle Processing");
            StartPlaying (FileBeingPlayed);
        }
    }
    // DEBUG_END;

} // TaskProcess

//-----------------------------------------------------------------------------
bool c_InputFPPRemote::Poll ()
{
    // DEBUG_START;
    bool Response = false;
    if(pInputFPPRemotePlayItem)
    {
        Response = pInputFPPRemotePlayItem->Poll ();
    }

    // DEBUG_END;
    return Response;

} // Poll

//-----------------------------------------------------------------------------
void c_InputFPPRemote::ProcessButtonActions(c_ExternalInput::InputValue_t value)
{
    // DEBUG_START;

    if(c_ExternalInput::InputValue_t::longOn == value)
    {
        // DEBUG_V("flip the dark flag");
        StayDark = !StayDark;
        // DEBUG_V(String("StayDark: ") + String(StayDark));
    }
    else if(c_ExternalInput::InputValue_t::shortOn == value)
    {
        // DEBUG_V("Are we playing a local file?");
        PlayNextFile();
    }
    else if(c_ExternalInput::InputValue_t::off == value)
    {
        // DEBUG_V("Got input Off notification");
    }

    // DEBUG_END;
} // ProcessButtonActions

//-----------------------------------------------------------------------------
void c_InputFPPRemote::SetBufferInfo (uint32_t BufferSize)
{
    InputDataBufferSize = BufferSize;

} // SetBufferInfo

//-----------------------------------------------------------------------------
bool c_InputFPPRemote::SetConfig (JsonObject& jsonConfig)
{
    // DEBUG_START;

    String FileToPlay;
    bool BlankOnStop = FPPDiscovery.GetBlankOnStop();
    setFromJSON (FileToPlay,   jsonConfig, JSON_NAME_FILE_TO_PLAY);
    setFromJSON (SyncOffsetMS, jsonConfig, CN_SyncOffset);
    setFromJSON (SendFppSync,  jsonConfig, CN_SendFppSync);
    setFromJSON (BlankOnStop,  jsonConfig, CN_blankOnStop);
    FPPDiscovery.SetBlankOnStop(BlankOnStop);

    if (PlayingFile())
    {
        pInputFPPRemotePlayItem->SetSyncOffsetMS (SyncOffsetMS);
        pInputFPPRemotePlayItem->SetSendFppSync (SendFppSync);
    }

    // DEBUG_V ("Config Processing");
    // Clear outbuffer on config change
    memset (OutputMgr.GetBufferAddress (), 0x0, OutputMgr.GetBufferUsedSize ());
    StartPlaying (FileToPlay);

    // DEBUG_END;

    return true;
} // SetConfig

//-----------------------------------------------------------------------------
void c_InputFPPRemote::StopPlaying ()
{
    // DEBUG_START;
    do // once
    {
        if (!PlayingFile ())
        {
            // DEBUG_V ("Not currently playing a file");
            break;
        }

        // handle re entrancy
        if(Stopping)
        {
            // already in the process of stopping
            break;
        }
        Stopping = true;

        // DEBUG_V ();
        FPPDiscovery.Disable ();
        FPPDiscovery.ForgetInputFPPRemotePlayFile ();

        if(PlayingFile())
        {
            // DEBUG_V(String("pInputFPPRemotePlayItem: ") = String(uint32_t(pInputFPPRemotePlayItem), HEX));
            pInputFPPRemotePlayItem->Stop ();

            while (!pInputFPPRemotePlayItem->IsIdle ())
            {
                pInputFPPRemotePlayItem->Poll ();
                // DEBUG_V();
                pInputFPPRemotePlayItem->Stop ();
            }

            // DEBUG_V("Delete current playing file");
            delete pInputFPPRemotePlayItem;
            pInputFPPRemotePlayItem = nullptr;
        }

        Stopping = false;

    } while (false);

    // DEBUG_END;

} // StopPlaying

//-----------------------------------------------------------------------------
void c_InputFPPRemote::StartPlaying (String& FileName)
{
    // DEBUG_START;

    do // once
    {
        // DEBUG_V (String ("FileName: '") + FileName + "'");
        if ((FileName.isEmpty ()) ||
            (FileName.equals("null")))
        {
            // DEBUG_V ("No file to play");
            StopPlaying ();
            break;
        }

        if (FileName.equals(No_LocalFileToPlay))
        {
            StartPlayingRemoteFile (FileName);
        }
        else
        {
            StartPlayingLocalFile (FileName);
        }

    } while (false);

    // DEBUG_END;

} // StartPlaying

//-----------------------------------------------------------------------------
void c_InputFPPRemote::StartPlayingLocalFile (String& FileName)
{
    // DEBUG_START;

    do // once
    {
        // make sure we are stopped (clears pInputFPPRemotePlayItem)
        StopPlaying();

        // DEBUG_V ("Start A New File");
        int Last_dot_pl_Position = FileName.lastIndexOf(CN_Dotpl);
        String Last_pl_Text = FileName.substring(Last_dot_pl_Position);
        if (String(CN_Dotpl) == Last_pl_Text)
        {
            // DEBUG_V ("Start Playlist");
            if(pInputFPPRemotePlayItem)
            {
                delete pInputFPPRemotePlayItem;
                pInputFPPRemotePlayItem = nullptr;
            }
            pInputFPPRemotePlayItem = new c_InputFPPRemotePlayList (GetInputChannelId ());
            StatusType = F ("PlayList");
        }
        else
        {
            int Last_dot_fseq_Position = FileName.lastIndexOf(CN_Dotfseq);
            String Last_fseq_Text = FileName.substring(Last_dot_fseq_Position);
            if (String(CN_Dotfseq) != Last_fseq_Text)
            {
                logcon(String(F("File Name does not end with a valid .fseq or .pl extension: '")) + FileName + "'");
                StatusType = F("Invalid File Name");
                FileBeingPlayed.clear();
                break;
            }

            if(pInputFPPRemotePlayItem)
            {
                delete pInputFPPRemotePlayItem;
                pInputFPPRemotePlayItem = nullptr;
            }
            // DEBUG_V ("Start Local FSEQ file player");
            pInputFPPRemotePlayItem = new c_InputFPPRemotePlayFile (GetInputChannelId ());
            StatusType = CN_File;
        }

        // DEBUG_V (String ("FileName: '") + FileName + "'");
        // DEBUG_V ("Start Playing");
        pInputFPPRemotePlayItem->SetSyncOffsetMS (SyncOffsetMS);
        pInputFPPRemotePlayItem->SetSendFppSync (SendFppSync);
        pInputFPPRemotePlayItem->Start (FileName, 0, 1);
        FileBeingPlayed = FileName;

    } while (false);

    // DEBUG_END;

} // StartPlayingLocalFile

//-----------------------------------------------------------------------------
void c_InputFPPRemote::StartPlayingRemoteFile (String& FileName)
{
    // DEBUG_START;

    do // once
    {
        if (PlayingRemoteFile ())
        {
            // DEBUG_V ("Already Playing in remote mode");
            break;
        }

        StopPlaying ();

        // DEBUG_V ("Instantiate an FSEQ file player");
        if(pInputFPPRemotePlayItem)
        {
            delete pInputFPPRemotePlayItem;
            pInputFPPRemotePlayItem = nullptr;
        }
        pInputFPPRemotePlayItem = new c_InputFPPRemotePlayFile (GetInputChannelId ());
        pInputFPPRemotePlayItem->SetSyncOffsetMS (SyncOffsetMS);
        pInputFPPRemotePlayItem->SetSendFppSync (SendFppSync);
        StatusType = CN_File;
        FileBeingPlayed = FileName;

        FPPDiscovery.SetInputFPPRemotePlayFile ((c_InputFPPRemotePlayFile *) pInputFPPRemotePlayItem);
        FPPDiscovery.Enable ();

    } while (false);

    // DEBUG_END;

} // StartPlaying

//-----------------------------------------------------------------------------
void c_InputFPPRemote::validateConfiguration ()
{
    // DEBUG_START;
    // DEBUG_END;

} // validate

//-----------------------------------------------------------------------------
bool c_InputFPPRemote::PlayingFile ()
{
    // DEBUG_START;

    bool response = false;

    do // once
    {
        if (nullptr == pInputFPPRemotePlayItem)
        {
            break;
        }

        response = true;
    } while (false);

    // DEBUG_END;
    return response;

} // PlayingFile

//-----------------------------------------------------------------------------
bool c_InputFPPRemote::PlayingRemoteFile ()
{
    // DEBUG_START;

    bool response = false;

    do // once
    {
        if (!PlayingFile ())
        {
            break;
        }

        if (!FileBeingPlayed.equals(No_LocalFileToPlay))
        {
            break;
        }

        response = true;

    } while (false);

    // DEBUG_END;
    return response;

} // PlayingRemoteFile
