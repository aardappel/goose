// bce: a growth of the array a reference parameter names may be a growth of
// any global array, so it bumps the length of each; carrying those lengths
// over must not crowd this body's own facts out of the table. The program
// has more global arrays than the table has room for facts.
fn escaped(out: u8[>..]&, s: u8[:]) {
    var start = 0;
    for c, i in s {
        if c != '"' { continue; }
        out.append(s[start..i]);
        start = i + 1;
        out.push('\\');
        out.push(c);
    }
    out.append(s[start..]);                           // bce:elide
}

var g0: u8[>..]; var g1: u8[>..]; var g2: u8[>..]; var g3: u8[>..]; var g4: u8[>..]; var g5: u8[>..]; var g6: u8[>..]; var g7: u8[>..];
var g8: u8[>..]; var g9: u8[>..]; var g10: u8[>..]; var g11: u8[>..]; var g12: u8[>..]; var g13: u8[>..]; var g14: u8[>..]; var g15: u8[>..];
var g16: u8[>..]; var g17: u8[>..]; var g18: u8[>..]; var g19: u8[>..]; var g20: u8[>..]; var g21: u8[>..]; var g22: u8[>..]; var g23: u8[>..];
var g24: u8[>..]; var g25: u8[>..]; var g26: u8[>..]; var g27: u8[>..]; var g28: u8[>..]; var g29: u8[>..]; var g30: u8[>..]; var g31: u8[>..];
var g32: u8[>..]; var g33: u8[>..]; var g34: u8[>..]; var g35: u8[>..]; var g36: u8[>..]; var g37: u8[>..]; var g38: u8[>..]; var g39: u8[>..];
var g40: u8[>..]; var g41: u8[>..]; var g42: u8[>..]; var g43: u8[>..]; var g44: u8[>..]; var g45: u8[>..]; var g46: u8[>..]; var g47: u8[>..];
var g48: u8[>..]; var g49: u8[>..]; var g50: u8[>..]; var g51: u8[>..]; var g52: u8[>..]; var g53: u8[>..]; var g54: u8[>..]; var g55: u8[>..];
var g56: u8[>..]; var g57: u8[>..]; var g58: u8[>..]; var g59: u8[>..]; var g60: u8[>..]; var g61: u8[>..]; var g62: u8[>..]; var g63: u8[>..];
var g64: u8[>..]; var g65: u8[>..]; var g66: u8[>..]; var g67: u8[>..]; var g68: u8[>..]; var g69: u8[>..]; var g70: u8[>..]; var g71: u8[>..];
var g72: u8[>..]; var g73: u8[>..]; var g74: u8[>..]; var g75: u8[>..]; var g76: u8[>..]; var g77: u8[>..]; var g78: u8[>..]; var g79: u8[>..];
var g80: u8[>..]; var g81: u8[>..]; var g82: u8[>..]; var g83: u8[>..]; var g84: u8[>..]; var g85: u8[>..]; var g86: u8[>..]; var g87: u8[>..];
var g88: u8[>..]; var g89: u8[>..]; var g90: u8[>..]; var g91: u8[>..]; var g92: u8[>..]; var g93: u8[>..]; var g94: u8[>..]; var g95: u8[>..];
var g96: u8[>..]; var g97: u8[>..]; var g98: u8[>..]; var g99: u8[>..]; var g100: u8[>..]; var g101: u8[>..]; var g102: u8[>..]; var g103: u8[>..];
var g104: u8[>..]; var g105: u8[>..]; var g106: u8[>..]; var g107: u8[>..]; var g108: u8[>..]; var g109: u8[>..]; var g110: u8[>..]; var g111: u8[>..];
var g112: u8[>..]; var g113: u8[>..]; var g114: u8[>..]; var g115: u8[>..]; var g116: u8[>..]; var g117: u8[>..]; var g118: u8[>..]; var g119: u8[>..];
var g120: u8[>..]; var g121: u8[>..]; var g122: u8[>..]; var g123: u8[>..]; var g124: u8[>..]; var g125: u8[>..]; var g126: u8[>..]; var g127: u8[>..];
var g128: u8[>..]; var g129: u8[>..]; var g130: u8[>..]; var g131: u8[>..]; var g132: u8[>..]; var g133: u8[>..]; var g134: u8[>..]; var g135: u8[>..];
var g136: u8[>..]; var g137: u8[>..]; var g138: u8[>..]; var g139: u8[>..]; var g140: u8[>..]; var g141: u8[>..]; var g142: u8[>..]; var g143: u8[>..];
var g144: u8[>..]; var g145: u8[>..]; var g146: u8[>..]; var g147: u8[>..]; var g148: u8[>..]; var g149: u8[>..]; var g150: u8[>..]; var g151: u8[>..];
var g152: u8[>..]; var g153: u8[>..]; var g154: u8[>..]; var g155: u8[>..]; var g156: u8[>..]; var g157: u8[>..]; var g158: u8[>..]; var g159: u8[>..];
var g160: u8[>..]; var g161: u8[>..]; var g162: u8[>..]; var g163: u8[>..]; var g164: u8[>..]; var g165: u8[>..]; var g166: u8[>..]; var g167: u8[>..];
var g168: u8[>..]; var g169: u8[>..]; var g170: u8[>..]; var g171: u8[>..]; var g172: u8[>..]; var g173: u8[>..]; var g174: u8[>..]; var g175: u8[>..];
var g176: u8[>..]; var g177: u8[>..]; var g178: u8[>..]; var g179: u8[>..]; var g180: u8[>..]; var g181: u8[>..]; var g182: u8[>..]; var g183: u8[>..];
var g184: u8[>..]; var g185: u8[>..]; var g186: u8[>..]; var g187: u8[>..]; var g188: u8[>..]; var g189: u8[>..]; var g190: u8[>..]; var g191: u8[>..];
var g192: u8[>..]; var g193: u8[>..]; var g194: u8[>..]; var g195: u8[>..]; var g196: u8[>..]; var g197: u8[>..]; var g198: u8[>..]; var g199: u8[>..];
var g200: u8[>..]; var g201: u8[>..]; var g202: u8[>..]; var g203: u8[>..]; var g204: u8[>..]; var g205: u8[>..]; var g206: u8[>..]; var g207: u8[>..];
var g208: u8[>..]; var g209: u8[>..]; var g210: u8[>..]; var g211: u8[>..]; var g212: u8[>..]; var g213: u8[>..]; var g214: u8[>..]; var g215: u8[>..];
var g216: u8[>..]; var g217: u8[>..]; var g218: u8[>..]; var g219: u8[>..]; var g220: u8[>..]; var g221: u8[>..]; var g222: u8[>..]; var g223: u8[>..];
var g224: u8[>..]; var g225: u8[>..]; var g226: u8[>..]; var g227: u8[>..]; var g228: u8[>..]; var g229: u8[>..]; var g230: u8[>..]; var g231: u8[>..];
var g232: u8[>..]; var g233: u8[>..]; var g234: u8[>..]; var g235: u8[>..]; var g236: u8[>..]; var g237: u8[>..]; var g238: u8[>..]; var g239: u8[>..];

fn spread() -> i64 {
    var t = 0;
    g0.push(0); g1.push(1); g2.push(2); g3.push(3); g4.push(4); g5.push(5); g6.push(6); g7.push(7);
    t += g0[0]; t += g1[0]; t += g2[0]; t += g3[0]; t += g4[0]; t += g5[0]; t += g6[0]; t += g7[0];
    g8.push(8); g9.push(9); g10.push(10); g11.push(11); g12.push(12); g13.push(13); g14.push(14); g15.push(15);
    t += g8[0]; t += g9[0]; t += g10[0]; t += g11[0]; t += g12[0]; t += g13[0]; t += g14[0]; t += g15[0];
    g16.push(16); g17.push(17); g18.push(18); g19.push(19); g20.push(20); g21.push(21); g22.push(22); g23.push(23);
    t += g16[0]; t += g17[0]; t += g18[0]; t += g19[0]; t += g20[0]; t += g21[0]; t += g22[0]; t += g23[0];
    g24.push(24); g25.push(25); g26.push(26); g27.push(27); g28.push(28); g29.push(29); g30.push(30); g31.push(31);
    t += g24[0]; t += g25[0]; t += g26[0]; t += g27[0]; t += g28[0]; t += g29[0]; t += g30[0]; t += g31[0];
    g32.push(32); g33.push(33); g34.push(34); g35.push(35); g36.push(36); g37.push(37); g38.push(38); g39.push(39);
    t += g32[0]; t += g33[0]; t += g34[0]; t += g35[0]; t += g36[0]; t += g37[0]; t += g38[0]; t += g39[0];
    g40.push(40); g41.push(41); g42.push(42); g43.push(43); g44.push(44); g45.push(45); g46.push(46); g47.push(47);
    t += g40[0]; t += g41[0]; t += g42[0]; t += g43[0]; t += g44[0]; t += g45[0]; t += g46[0]; t += g47[0];
    g48.push(48); g49.push(49); g50.push(50); g51.push(51); g52.push(52); g53.push(53); g54.push(54); g55.push(55);
    t += g48[0]; t += g49[0]; t += g50[0]; t += g51[0]; t += g52[0]; t += g53[0]; t += g54[0]; t += g55[0];
    g56.push(56); g57.push(57); g58.push(58); g59.push(59); g60.push(60); g61.push(61); g62.push(62); g63.push(63);
    t += g56[0]; t += g57[0]; t += g58[0]; t += g59[0]; t += g60[0]; t += g61[0]; t += g62[0]; t += g63[0];
    g64.push(64); g65.push(65); g66.push(66); g67.push(67); g68.push(68); g69.push(69); g70.push(70); g71.push(71);
    t += g64[0]; t += g65[0]; t += g66[0]; t += g67[0]; t += g68[0]; t += g69[0]; t += g70[0]; t += g71[0];
    g72.push(72); g73.push(73); g74.push(74); g75.push(75); g76.push(76); g77.push(77); g78.push(78); g79.push(79);
    t += g72[0]; t += g73[0]; t += g74[0]; t += g75[0]; t += g76[0]; t += g77[0]; t += g78[0]; t += g79[0];
    g80.push(80); g81.push(81); g82.push(82); g83.push(83); g84.push(84); g85.push(85); g86.push(86); g87.push(87);
    t += g80[0]; t += g81[0]; t += g82[0]; t += g83[0]; t += g84[0]; t += g85[0]; t += g86[0]; t += g87[0];
    g88.push(88); g89.push(89); g90.push(90); g91.push(91); g92.push(92); g93.push(93); g94.push(94); g95.push(95);
    t += g88[0]; t += g89[0]; t += g90[0]; t += g91[0]; t += g92[0]; t += g93[0]; t += g94[0]; t += g95[0];
    g96.push(96); g97.push(97); g98.push(98); g99.push(99); g100.push(100); g101.push(101); g102.push(102); g103.push(103);
    t += g96[0]; t += g97[0]; t += g98[0]; t += g99[0]; t += g100[0]; t += g101[0]; t += g102[0]; t += g103[0];
    g104.push(104); g105.push(105); g106.push(106); g107.push(107); g108.push(108); g109.push(109); g110.push(110); g111.push(111);
    t += g104[0]; t += g105[0]; t += g106[0]; t += g107[0]; t += g108[0]; t += g109[0]; t += g110[0]; t += g111[0];
    g112.push(112); g113.push(113); g114.push(114); g115.push(115); g116.push(116); g117.push(117); g118.push(118); g119.push(119);
    t += g112[0]; t += g113[0]; t += g114[0]; t += g115[0]; t += g116[0]; t += g117[0]; t += g118[0]; t += g119[0];
    g120.push(120); g121.push(121); g122.push(122); g123.push(123); g124.push(124); g125.push(125); g126.push(126); g127.push(127);
    t += g120[0]; t += g121[0]; t += g122[0]; t += g123[0]; t += g124[0]; t += g125[0]; t += g126[0]; t += g127[0];
    g128.push(128); g129.push(129); g130.push(130); g131.push(131); g132.push(132); g133.push(133); g134.push(134); g135.push(135);
    t += g128[0]; t += g129[0]; t += g130[0]; t += g131[0]; t += g132[0]; t += g133[0]; t += g134[0]; t += g135[0];
    g136.push(136); g137.push(137); g138.push(138); g139.push(139); g140.push(140); g141.push(141); g142.push(142); g143.push(143);
    t += g136[0]; t += g137[0]; t += g138[0]; t += g139[0]; t += g140[0]; t += g141[0]; t += g142[0]; t += g143[0];
    g144.push(144); g145.push(145); g146.push(146); g147.push(147); g148.push(148); g149.push(149); g150.push(150); g151.push(151);
    t += g144[0]; t += g145[0]; t += g146[0]; t += g147[0]; t += g148[0]; t += g149[0]; t += g150[0]; t += g151[0];
    g152.push(152); g153.push(153); g154.push(154); g155.push(155); g156.push(156); g157.push(157); g158.push(158); g159.push(159);
    t += g152[0]; t += g153[0]; t += g154[0]; t += g155[0]; t += g156[0]; t += g157[0]; t += g158[0]; t += g159[0];
    g160.push(160); g161.push(161); g162.push(162); g163.push(163); g164.push(164); g165.push(165); g166.push(166); g167.push(167);
    t += g160[0]; t += g161[0]; t += g162[0]; t += g163[0]; t += g164[0]; t += g165[0]; t += g166[0]; t += g167[0];
    g168.push(168); g169.push(169); g170.push(170); g171.push(171); g172.push(172); g173.push(173); g174.push(174); g175.push(175);
    t += g168[0]; t += g169[0]; t += g170[0]; t += g171[0]; t += g172[0]; t += g173[0]; t += g174[0]; t += g175[0];
    g176.push(176); g177.push(177); g178.push(178); g179.push(179); g180.push(180); g181.push(181); g182.push(182); g183.push(183);
    t += g176[0]; t += g177[0]; t += g178[0]; t += g179[0]; t += g180[0]; t += g181[0]; t += g182[0]; t += g183[0];
    g184.push(184); g185.push(185); g186.push(186); g187.push(187); g188.push(188); g189.push(189); g190.push(190); g191.push(191);
    t += g184[0]; t += g185[0]; t += g186[0]; t += g187[0]; t += g188[0]; t += g189[0]; t += g190[0]; t += g191[0];
    g192.push(192); g193.push(193); g194.push(194); g195.push(195); g196.push(196); g197.push(197); g198.push(198); g199.push(199);
    t += g192[0]; t += g193[0]; t += g194[0]; t += g195[0]; t += g196[0]; t += g197[0]; t += g198[0]; t += g199[0];
    g200.push(0); g201.push(1); g202.push(2); g203.push(3); g204.push(4); g205.push(5); g206.push(6); g207.push(7);
    t += g200[0]; t += g201[0]; t += g202[0]; t += g203[0]; t += g204[0]; t += g205[0]; t += g206[0]; t += g207[0];
    g208.push(8); g209.push(9); g210.push(10); g211.push(11); g212.push(12); g213.push(13); g214.push(14); g215.push(15);
    t += g208[0]; t += g209[0]; t += g210[0]; t += g211[0]; t += g212[0]; t += g213[0]; t += g214[0]; t += g215[0];
    g216.push(16); g217.push(17); g218.push(18); g219.push(19); g220.push(20); g221.push(21); g222.push(22); g223.push(23);
    t += g216[0]; t += g217[0]; t += g218[0]; t += g219[0]; t += g220[0]; t += g221[0]; t += g222[0]; t += g223[0];
    g224.push(24); g225.push(25); g226.push(26); g227.push(27); g228.push(28); g229.push(29); g230.push(30); g231.push(31);
    t += g224[0]; t += g225[0]; t += g226[0]; t += g227[0]; t += g228[0]; t += g229[0]; t += g230[0]; t += g231[0];
    g232.push(32); g233.push(33); g234.push(34); g235.push(35); g236.push(36); g237.push(37); g238.push(38); g239.push(39);
    t += g232[0]; t += g233[0]; t += g234[0]; t += g235[0]; t += g236[0]; t += g237[0]; t += g238[0]; t += g239[0];
    return t;
}

fn main() {
    var out: u8[>..] = [];
    escaped(out, "a\"b\"c");
    print(out, " ", spread());
}
