#pragma once
struct controller_data_s
{
    int start, a, b, x, y, lb, rb;
    int rt, lt;
    int up, down, left, right;
    int s1_x, s1_y, s2_x, s2_y;
    int back, logo;
};
void get_controller_data(struct controller_data_s * c, int Control);
