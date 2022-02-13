--[[
/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */
]]
do
--[[
  this creates a bit-encoded lookup table to generate 
  bbox vertices depending on "to camera" direction
  
  the reference index buffer is generated for this bbox
  direction signs: + + +
  
  index_buffer[] = {
    2, 3, 7,
    7, 3, 1, 
    1, 5, 7, 
    7, 5, 4, 
    4, 6, 7, 
    7, 6, 2
  };
  
    4_____5
   /.    /|
  6_____7 |
  | 0...|.1
  |.    |/
  2_____3

  z negative means we need to rotate the vertices a bit
  to match this topology pattern again (the tip is always vertex 7)
  
]]

  local table = 
  {
    -- direction signs: - - -
      {7,5,6,4,3,1,2,0},
    -- direction signs: + - -
      {6,7,4,5,2,3,0,1},
    -- direction signs: - + -
      {5,4,7,6,1,0,3,2},
    -- direction signs: + + -
      {4,6,5,7,0,2,1,3},
    -- direction signs: - - +
      {3,2,1,0,7,6,5,4},
    -- direction signs: + - +
      {2,0,3,1,6,4,7,5},
    -- direction signs: - + +
      {1,3,0,2,5,7,4,6},
    -- direction signs: + + +
      {0,1,2,3,4,5,6,7},
  }
  
  local lowerBits = {0,0,0,0}
  local upperBits = {0,0,0,0}
  
  local bit32 = require "bit"
  
  for dir=0,7 do
    for v=0,7 do
      local vtx = table[dir+1][v+1]
      
      local dirHalf = math.floor(dir / 2)
      
      if (dir < 4) then
        lowerBits[dir + 1] = bit32.bor(lowerBits[dir + 1], bit32.lshift(vtx, v * 4))
      else
        upperBits[dir + 1 - 4] = bit32.bor(upperBits[dir + 1 - 4], bit32.lshift(vtx, v * 4))
      end
    end
  end
  
  print(string.format("uvec4(0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu);", lowerBits[1],lowerBits[2],lowerBits[3],lowerBits[4]))
  print(string.format("uvec4(0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu);", upperBits[1],upperBits[2],upperBits[3],upperBits[4]))
  
end