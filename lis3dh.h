
#ifndef LIS3DH_H
#define LIS3DH_H

void lis3dh_init(void);
void lis3dh_read(uint8_t addr, uint8_t *data, uint8_t size);

#endif  //LIS3DH_H
