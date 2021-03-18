do
  local f = io.open("test.txt", "wt")
  local function fw(str)
    f:write(str.."\n")
  end
  
  --fw("-noui")
  fw("-benchmarkframes 4")
  fw("-vsync 0")
  fw("-culling 1")
  
  local drawmodes = 4
  local results = 3
  local methods = 3
  
  -- functional
  for m=0,methods-1 do
    for d=0,drawmodes-1 do
      for r=0,results-1 do
        fw("-animateoffset "..(m*methods+d*drawmodes+r*results))
        fw("-method "..m)
        fw("-drawmode "..d)
        fw("-result "..r)
        fw('benchmark "m'..m.."d"..d.."r"..r..'"')
        fw('-screenshot "test_m'..m.."d"..d.."r"..r..'.bmp"')
      end
    end
  end
  
  fw("-animateoffset 0")
  fw("-method 2")
  fw("-result 2")
  
  -- permutation of drawmodes
  for from=0,drawmodes-1 do
    for to=0,drawmodes-1 do
      fw("-drawmode "..from)
      fw('benchmark "a_fd'..from.."td"..to..'"')
      fw("-drawmode "..to)
      fw('benchmark "b_fd'..from.."td"..to..'"')
    end
  end
  
  f:close()
end