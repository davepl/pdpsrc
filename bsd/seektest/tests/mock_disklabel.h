/* Host-only stand-in for the fields used by the native disklabel API. */
#define MAXPARTITIONS 8
#define DIOCGDINFO 0x1234
struct partition {
    unsigned long p_size, p_offset;
};
struct disklabel {
    unsigned short d_secsize, d_secpercyl, d_npartitions;
    unsigned long d_secperunit;
    struct partition d_partitions[MAXPARTITIONS];
};
