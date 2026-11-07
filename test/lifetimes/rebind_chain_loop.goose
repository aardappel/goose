// A loop that hands a slice down a chain of variables, one variable per
// iteration: each pass of the loop's check moves the root one variable
// further, so it takes a pass per variable to settle.
fn main() {
    var n = 0;
    var a0:  i64[1] = [0];
    var a1:  i64[1] = [1];
    var a2:  i64[1] = [2];
    var a3:  i64[1] = [3];
    var a4:  i64[1] = [4];
    var a5:  i64[1] = [5];
    var a6:  i64[1] = [6];
    var a7:  i64[1] = [7];
    var a8:  i64[1] = [8];
    var a9:  i64[1] = [9];
    var a10: i64[1] = [10];
    var a11: i64[1] = [11];
    var a12: i64[1] = [12];
    var a13: i64[1] = [13];
    var a14: i64[1] = [14];
    var a15: i64[1] = [15];
    var a16: i64[1] = [16];
    var a17: i64[1] = [17];
    var a18: i64[1] = [18];
    var a19: i64[1] = [19];
    var a20: i64[1] = [20];
    var v0:  i64[:] = a0;
    var v1:  i64[:] = a1;
    var v2:  i64[:] = a2;
    var v3:  i64[:] = a3;
    var v4:  i64[:] = a4;
    var v5:  i64[:] = a5;
    var v6:  i64[:] = a6;
    var v7:  i64[:] = a7;
    var v8:  i64[:] = a8;
    var v9:  i64[:] = a9;
    var v10: i64[:] = a10;
    var v11: i64[:] = a11;
    var v12: i64[:] = a12;
    var v13: i64[:] = a13;
    var v14: i64[:] = a14;
    var v15: i64[:] = a15;
    var v16: i64[:] = a16;
    var v17: i64[:] = a17;
    var v18: i64[:] = a18;
    var v19: i64[:] = a19;
    while n < 3 {
        v0 = v1; v1 = v2; v2 = v3; v3 = v4; v4 = v5;
        v5 = v6; v6 = v7; v7 = v8; v8 = v9; v9 = v10;
        v10 = v11; v11 = v12; v12 = v13; v13 = v14; v14 = v15;
        v15 = v16; v16 = v17; v17 = v18; v18 = v19;
        v19 = a20;
        n += 1;
    }
    print(v0[0]);
    print(v16[0]);
    print(v19[0]);
}
