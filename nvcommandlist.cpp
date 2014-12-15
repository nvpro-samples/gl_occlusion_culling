/*-----------------------------------------------------------------------
  Copyright (c) 2014, NVIDIA. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Neither the name of its contributors may be used to endorse 
     or promote products derived from this software without specific
     prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

#include "nvcommandlist.h"

PFNGLCREATESTATESNVPROC __nvcCreateStatesNV;
PFNGLDELETESTATESNVPROC __nvcDeleteStatesNV;
PFNGLISSTATENVPROC __nvcIsStateNV;
PFNGLSTATECAPTURENVPROC __nvcStateCaptureNV;
PFNGLDRAWCOMMANDSNVPROC __nvcDrawCommandsNV;
PFNGLDRAWCOMMANDSADDRESSNVPROC __nvcDrawCommandsAddressNV;
PFNGLDRAWCOMMANDSSTATESNVPROC __nvcDrawCommandsStatesNV;
PFNGLDRAWCOMMANDSSTATESADDRESSNVPROC __nvcDrawCommandsStatesAddressNV;
PFNGLCREATECOMMANDLISTSNVPROC __nvcCreateCommandListsNV;
PFNGLDELETECOMMANDLISTSNVPROC __nvcDeleteCommandListsNV;
PFNGLISCOMMANDLISTNVPROC __nvcIsCommandListNV;
PFNGLLISTDRAWCOMMANDSSTATESCLIENTNVPROC __nvcListDrawCommandsStatesClientNV;
PFNGLCOMMANDLISTSEGMENTSNVPROC __nvcCommandListSegmentsNV;
PFNGLCOMPILECOMMANDLISTNVPROC __nvcCompileCommandListNV;
PFNGLCALLCOMMANDLISTNVPROC __nvcCallCommandListNV;
PFNGLGETCOMMANDHEADERNVPROC __nvcGetCommandHeaderNV;
PFNGLGETSTAGEINDEXNVPROC __nvcGetStageIndexNV;


static int initedNVcommandList = 0;

int init_NV_command_list(NVCPROC (*fnGetProc)(const char* name))
{
  if (initedNVcommandList) return __nvcCreateStatesNV != ((void*)0);

  __nvcCreateStatesNV = (PFNGLCREATESTATESNVPROC)fnGetProc("glCreateStatesNV");
  __nvcDeleteStatesNV = (PFNGLDELETESTATESNVPROC)fnGetProc("glDeleteStatesNV");
  __nvcIsStateNV = (PFNGLISSTATENVPROC)fnGetProc("glIsStateNV");
  __nvcStateCaptureNV = (PFNGLSTATECAPTURENVPROC)fnGetProc("glStateCaptureNV");
  __nvcDrawCommandsNV = (PFNGLDRAWCOMMANDSNVPROC)fnGetProc("glDrawCommandsNV");
  __nvcDrawCommandsAddressNV = (PFNGLDRAWCOMMANDSADDRESSNVPROC)fnGetProc("glDrawCommandsAddressNV");
  __nvcDrawCommandsStatesNV = (PFNGLDRAWCOMMANDSSTATESNVPROC)fnGetProc("glDrawCommandsStatesNV");
  __nvcDrawCommandsStatesAddressNV = (PFNGLDRAWCOMMANDSSTATESADDRESSNVPROC)fnGetProc("glDrawCommandsStatesAddressNV");
  __nvcCreateCommandListsNV = (PFNGLCREATECOMMANDLISTSNVPROC)fnGetProc("glCreateCommandListsNV");
  __nvcDeleteCommandListsNV = (PFNGLDELETECOMMANDLISTSNVPROC)fnGetProc("glDeleteCommandListsNV");
  __nvcIsCommandListNV = (PFNGLISCOMMANDLISTNVPROC)fnGetProc("glIsCommandListNV");
  __nvcListDrawCommandsStatesClientNV = (PFNGLLISTDRAWCOMMANDSSTATESCLIENTNVPROC)fnGetProc("glListDrawCommandsStatesClientNV");
  __nvcCommandListSegmentsNV = (PFNGLCOMMANDLISTSEGMENTSNVPROC)fnGetProc("glCommandListSegmentsNV");
  __nvcCompileCommandListNV = (PFNGLCOMPILECOMMANDLISTNVPROC)fnGetProc("glCompileCommandListNV");
  __nvcCallCommandListNV = (PFNGLCALLCOMMANDLISTNVPROC)fnGetProc("glCallCommandListNV");
  __nvcGetCommandHeaderNV = (PFNGLGETCOMMANDHEADERNVPROC)fnGetProc("glGetCommandHeaderNV");
  __nvcGetStageIndexNV = (PFNGLGETSTAGEINDEXNVPROC)fnGetProc("glGetStageIndexNV");
  
  initedNVcommandList = 1;
  
  return __nvcCreateStatesNV != ((void*)0);
}

