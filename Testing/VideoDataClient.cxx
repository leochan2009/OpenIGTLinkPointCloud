/*=========================================================================

  Program:   Open IGT Link -- Example for Tracker Client Program
  Module:    $RCSfile: $
  Language:  C++
  Date:      $Date: $
  Version:   $Revision: $

  Copyright (c) Insight Software Consortium. All rights reserved.

  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#include <fstream>
#include "api/svc/codec_api.h"
#include "api/svc/codec_app_def.h"
#include "utils/BufferedData.h"
#include "utils/FileInputStream.h"
#include "api/sha1.c"
#include "commonFunctions.h"


#include "igtlOSUtil.h"
#include "igtlMessageHeader.h"
#include "igtlVideoMessage.h"
#include "igtlServerSocket.h"
#include "igtlMultiThreader.h"


int ReceiveVideoData(igtl::ClientSocket::Pointer& socket, igtl::VideoMessageHeader::Pointer& header, ISVCDecoder* decoder_, const char* outputFileName);

int main(int argc, char* argv[])
{
  //------------------------------------------------------------
  // Parse Arguments

  if (argc != 4) // check number of arguments
    {
    // If not correct, print usage
    std::cerr << "Usage: " << argv[0] << " <hostname> <port> <fps>"    << std::endl;
    std::cerr << "    <hostname> : IP or host name"                    << std::endl;
    std::cerr << "    <port>     : Port # (18944 in Slicer default)"   << std::endl;
    std::cerr << "    <fps>      : Frequency (fps) to send frame" << std::endl;
    exit(0);
    }

  char*  hostname = argv[1];
  int    port     = atoi(argv[2]);
  double fps      = atof(argv[3]);
  int    interval = (int) (1000.0 / fps);
  
  
  ISVCDecoder* decoder_;
  WelsCreateDecoder (&decoder_);
  SDecodingParam decParam;
  memset (&decParam, 0, sizeof (SDecodingParam));
  decParam.uiTargetDqLayer = UCHAR_MAX;
  decParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;
  decParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
  decoder_->Initialize (&decParam);
  bool decodeFromFileStream = false;
  if (decodeFromFileStream)
  {
    int32_t iWidth = 0;
    int32_t iHeight = 0;
    unsigned char * pBuf = NULL;
    FILE* pH264File   = NULL;
    pH264File = fopen ("/Users/longquanchen/Desktop/Github/OpenIGTLink-xcodeBuild/Testing/OpenH264/test_vd_rc.264", "rb");
    fseek (pH264File, 0L, SEEK_END);
    int iFileSize = (int32_t) ftell (pH264File);
    pBuf = new uint8_t[iFileSize + 4];
    fseek (pH264File, 0L, SEEK_SET);
    if (fread (pBuf, 1, iFileSize, pH264File) == (uint32_t)iFileSize)
      H264DecodeInstance (decoder_, pBuf , "outputDecodedFileStream.yuv" , iWidth, iHeight, iFileSize,  NULL);
    return 1;
  }
  //------------------------------------------------------------
  // Establish Connection

  igtl::ClientSocket::Pointer socket;
  socket = igtl::ClientSocket::New();
  int r = socket->ConnectToServer(hostname, port);

  if (r != 0)
    {
    std::cerr << "Cannot connect to the server." << std::endl;
    exit(0);
    }

  //------------------------------------------------------------
  // Ask the server to start pushing tracking data
  std::cerr << "Sending STT_VIDEO message....." << std::endl;
  igtl::StartVideoDataMessage::Pointer startVideoMsg;
  startVideoMsg = igtl::StartVideoDataMessage::New();
  startVideoMsg->SetDeviceName("Video Client");
  startVideoMsg->SetResolution(interval);
  startVideoMsg->Pack();
  socket->Send(startVideoMsg->GetPackPointer(), startVideoMsg->GetPackSize());
  int loop = 0;
  std::string outputFileName = "outputDecodedVideo.yuv";
  FILE* YUVFile = fopen (outputFileName.c_str(), "wb");
  fclose(YUVFile);
  while (1)
  {
    //------------------------------------------------------------
    // Wait for a reply
    igtl::VideoMessageHeader::Pointer headerMsg;
    headerMsg = igtl::VideoMessageHeader::New();
    headerMsg->InitPack();
    int rs = socket->Receive(headerMsg->GetPackPointer(), headerMsg->GetPackSize());
    if (rs == 0)
      {
      std::cerr << "Connection closed." << std::endl;
      socket->CloseSocket();
      exit(0);
      }
    if (rs != headerMsg->GetPackSize())
      {
      std::cerr << "Message size information and actual data size don't match." << std::endl; 
      socket->CloseSocket();
      exit(0);
      }
    
    headerMsg->Unpack();
    if (strcmp(headerMsg->GetDeviceType(), "VIDEO_HEADER") == 0)
    {
      ReceiveVideoData(socket, headerMsg, decoder_, outputFileName.c_str());
    }
    else
    {
      std::cerr << "Receiving : " << headerMsg->GetDeviceType() << std::endl;
      socket->Skip(headerMsg->GetBodySizeToRead(), 0);
    }
    if (++loop >= 16) // if received 16 times
    {
      //------------------------------------------------------------
      // Ask the server to stop pushing tracking data
      std::cerr << "Sending STP_VIDEO message....." << std::endl;
      igtl::StopVideoMessage::Pointer stopVideoMsg;
      stopVideoMsg = igtl::StopVideoMessage::New();
      stopVideoMsg->SetDeviceName("TDataClient");
      stopVideoMsg->Pack();
      socket->Send(stopVideoMsg->GetPackPointer(), stopVideoMsg->GetPackSize());
      loop = 0;
    }
  }
  WelsDestroyDecoder(decoder_);
}


int ReceiveVideoData(igtl::ClientSocket::Pointer& socket, igtl::VideoMessageHeader::Pointer& header, ISVCDecoder* decoder_, const char* outputFileName)
{
  std::cerr << "Receiving Video data type." << std::endl;
  
  //------------------------------------------------------------
  // Allocate Video Message Class

  igtl::VideoMessage::Pointer videoMsg;
  videoMsg = igtl::VideoMessage::New();
  videoMsg->SetMessageHeader(header);
  videoMsg->AllocatePack(header->GetBodyPackSize());

  // Receive body from the socket
  socket->Receive(videoMsg->GetPackBodyPointer(), videoMsg->GetPackBodySize());

  // Deserialize the transform data
  // If you want to skip CRC check, call Unpack() without argument.
  videoMsg->Unpack();

  if (igtl::MessageHeader::UNPACK_BODY)
  {
    //SFrameBSInfo * info  = (SFrameBSInfo *)videoMsg->GetPackFragmentPointer(2); //Here the m_Frame point is receive, the m_FrameHeader is at index 1, we need to check what information we need to put into the image header.
    uint8_t* data[3];
    SBufferInfo bufInfo;
    memset (data, 0, sizeof (data));
    memset (&bufInfo, 0, sizeof (SBufferInfo));
    int32_t iWidth = videoMsg->GetWidth(), iHeight = videoMsg->GetHeight(), streamLength = videoMsg->GetPackBodySize()- IGTL_VIDEO_HEADER_SIZE;
    //DECODING_STATE rv = decoder_->DecodeFrame( (const unsigned char *)info, iSrcLen, ppDst, pStride, pStride[0], pStride[1]);
    H264DecodeInstance(decoder_, videoMsg->GetPackFragmentPointer(2), outputFileName, iWidth, iHeight, streamLength, NULL);
    return 1;
  }
  return 0;
}