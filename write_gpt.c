#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <strings.h> //for strcasecmp
#include <uchar.h> //for UCS-2 (UTF-16) string support
#include <unistd.h> //for access(), fork(), execvp()
#include <sys/wait.h> //for waitpid()
//NOTE: This code is written for 512 byte sectors, and will not work for 4096 byte sectors without modification


// GUID/UUID structure
typedef struct {
    uint32_t time_lo;
    uint16_t time_mid;
    uint16_t time_hi_and_version; // Upper 4 bits are version #
    uint8_t clock_seq_hi_and_reserved; // Upper 2 bits are variant #
    uint8_t clock_seq_lo;
    uint8_t node[6];
} __attribute__((packed)) GUID;

// MBR Parition structure
typedef struct {
    uint8_t boot_indicator;
    uint8_t starting_chs[3];
    uint8_t os_type;
    uint8_t ending_chs[3];
    uint32_t starting_lba;
    uint32_t size_lba;
} __attribute__((packed)) Mbr_Partition;

//Master Boot Record structure
typedef struct {
    uint8_t boot_code[440];
    uint32_t mbr_signature;
    uint16_t unknown;
    Mbr_Partition partition[4];
    uint16_t boot_signature;
} __attribute__ ((packed)) Mbr;


typedef struct {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved_l;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    GUID disk_guid; //pseudorandomly generated Version 4 variant 2 GUID
    uint64_t partition_table_lba;
    uint32_t number_of_entries;
    uint32_t size_of_entry;
    uint32_t partition_table_crc32;

    uint8_t reserved_2[512-92];
} __attribute__((packed)) Gpt_Header;

typedef struct {
    GUID partition_type_guid;
    GUID unique_guid;
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    char16_t name[36]; //UCS-2 (UTF-16 limited to code points 0x0000-0xFFFF)
} __attribute__((packed)) Gpt_Partition_Entry;


//FAT32 Volume Boot Record (VBR) structure
typedef struct {
    uint8_t BS_jmpBoot[3];
    uint8_t BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    uint8_t BPB_Reserved[12];
    uint8_t BS_DrvNum;
    uint8_t BS_Reserved1;
    uint8_t BS_BootSig;
    uint32_t BS_VolID;
    uint8_t BS_VolLab[11];
    uint8_t BS_FilSysType[8];

    // Not in fatgen103.doc tables
    uint8_t boot_code[510-90];
    uint16_t bootsect_sig; //boot sector signature 0xAA55
} __attribute__((packed)) Fat32_Vbr;


typedef struct {
    uint32_t FSI_LeadSig;
    uint8_t FSI_Reserved1[480];
    uint32_t FSI_StructSig;
    uint32_t FSI_Free_Count;
    uint32_t FSI_Nxt_Free;
    uint8_t FSI_Reserved2[12];
    uint32_t FSI_TrailSig;
}__attribute__((packed)) FSInfo;


typedef struct {
    uint8_t  DIR_Name[11];
    uint8_t  DIR_Attr;
    uint8_t  DIR_NTRes;
    uint8_t  DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
}__attribute__((packed)) FAT32_Dir_Entry_Short;


// FAT32 Directory Entry Attributes
typedef enum {
    ATTR_READ_ONLY = 0x01,
    ATTR_HIDDEN    = 0x02,
    ATTR_SYSTEM    = 0x04,
    ATTR_VOLUME_ID = 0x08,
    ATTR_DIRECTORY = 0x10,
    ATTR_ARCHIVE   = 0x20,
    ATTR_LONG_NAME = ATTR_READ_ONLY | ATTR_HIDDEN |
                     ATTR_SYSTEM    | ATTR_VOLUME_ID,
} FAT32_Dir_Attr;


// FAT32 File "types"
typedef enum {
    TYPE_DIR,   // Directory
    TYPE_FILE,  // Regular file
} File_Type;


// Common Virtual Hard Disk Footer, for a "fixed" vhd
// All fields are in network byte order (Big Endian),
//   since I'm lazy or otherwise a bad programmer,
//   we'll use byte arrays here
typedef struct {
    uint8_t cookie[8];
    uint8_t features[4];
    uint8_t version[4];
    uint64_t data_offset;
    uint8_t timestamp[4];
    uint8_t creator_app[4];
    uint8_t creator_ver[4];
    uint8_t creator_OS[4];
    uint8_t original_size[8];
    uint8_t current_size[8];
    uint8_t disk_geometry[4];
    uint8_t disk_type[4];
    uint8_t checksum[4];
    GUID unique_id;
    uint8_t saved_state;
    uint8_t reserved[427];
} __attribute__ ((packed)) Vhd;


// Internal Options object for commandline args
typedef struct {
    char *image_name;
    uint32_t lba_size;
    uint32_t esp_size;
    uint32_t data_size;
    char **esp_file_paths;
    uint32_t num_esp_file_paths;
    FILE **esp_files;
    char **data_files;
    uint32_t num_data_files;
    bool vhd;
    bool help;
    bool error;
} Options;


//TODO: Find out why these const are written the way they are

// EFI System Partition GUID
const GUID ESP_GUID = {
    0xC12A7328, 0xF81F, 0x11D2, 0xBA, 0x4B,
    {0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
};


// (Microsoft) Basic Data Partition GUID
const GUID BASIC_DATA_GUID = {
    0xEBD0A0A2, 0xB9E5, 0x4433, 0x87, 0xC0,
    {0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}
};


enum {
    GPT_TABLE_ENTRY_SIZE = 128, //size of one partition entry in bytes
    NUMBER_OF_GPT_TABLE_ENTRIES = 128, //number of partition entries in the GPT table
    GPT_TABLE_SIZE = 16384, //Minimum size per UEFI spec 2.10, 128 entries * 128 bytes per entry
    ALIGNMENT = 1048576 //1MB alignment for GPT tables
};


char *image_name = "test.img";
uint64_t lba_size = 512;
uint64_t esp_size = 1024 * 1024 * 33;
uint64_t data_size = 1024 * 1024 * 1;
uint64_t image_size = 0;

uint64_t esp_size_lbas;
uint64_t data_size_lbas;
uint64_t image_size_lbas;

//Sizes of LBAs
uint64_t gpt_table_lbas = 0;

//LBA of GPT table
uint64_t align_lba = 0;
uint64_t esp_lba = 0;
uint64_t data_lba = 0;


//FAT32 information used while adding files
uint32_t fat_start_lba = 0;
uint32_t fat_size_lbas = 0;
uint32_t fat_data_lba = 0;
uint32_t next_free_cluster = 5;

//Bootloader file to embed in /EFI/BOOT/BOOTX64.EFI, and CLI flags
char *bootloader_path = NULL;
bool verbose_flag = false;

//Launch QEMU under OVMF after building the image
bool run_flag = false;
char *ovmf_code_path = NULL;
char *ovmf_vars_path = NULL;


//Convert bytes to number of LBAs
uint64_t bytes_to_lbas(const uint64_t bytes)
{
    return (bytes / lba_size) + (bytes % lba_size > 0 ? 1 : 0);
}


//Will fill the rest of image with 0s to make it a full LBA size
void write_full_lba_size(FILE *image)
{
    //TODO: Need this to handle 4096 byte sectors as well, but for now just assume 512 byte sectors
    long position = ftell(image);

    if (position < 0)
    {
        return;
    }

    uint32_t remainder = position % lba_size;

    if (remainder == 0)
    {
        return;
    }

    uint8_t zeros[512] = {0};
    uint32_t bytes_left = lba_size - remainder;

    fwrite(zeros, 1, bytes_left, image);
}


//Get the next 1MB aligned LBA
uint64_t next_aligned_lba(uint64_t lba)
{
    return ((lba + align_lba - 1) / align_lba) * align_lba;
}


//Generate a new random GUID
GUID new_guid(void)
{
    uint8_t rand_arr[16] = {0};

    for (uint8_t i = 0; i < sizeof(rand_arr); i++)
    {
        rand_arr[i] = rand() % 256; //Generates pesudorandom numbers between 0 and 255
    }

    GUID result = {
        //TODO: Make sure this is a Version 4 Variant 2 GUID
        .time_lo =
            ((uint32_t)rand_arr[0] << 24) |
            ((uint32_t)rand_arr[1] << 16) |
            ((uint32_t)rand_arr[2] << 8) |
            rand_arr[3],

        .time_mid =
            ((uint16_t)rand_arr[4] << 8) |
            rand_arr[5],

        .time_hi_and_version =
            ((uint16_t)rand_arr[6] << 8) |
            rand_arr[7],

        .clock_seq_hi_and_reserved = rand_arr[8],
        .clock_seq_lo = rand_arr[9],

        .node = {
            rand_arr[10],
            rand_arr[11],
            rand_arr[12],
            rand_arr[13],
            rand_arr[14],
            rand_arr[15]
        }
    };

    //Version bits (Version 4)
    result.time_hi_and_version &= 0x0FFF;
    result.time_hi_and_version |= 0x4000;

    //Variant bits (Variant 2)
    result.clock_seq_hi_and_reserved &= 0x3F;
    result.clock_seq_hi_and_reserved |= 0x80;

    return result;
}


uint32_t crc32_table[256];


void create_crc32_table(void)
{
    uint32_t c;
    int32_t n, k;

    for (n = 0; n < 256; n++)
    {
        c = (uint32_t)n;

        for (k = 0; k < 8; k++)
        {
            if (c & 1)
                c = 0xEDB88320L ^ (c >> 1);
            else
                c = c >> 1;
        }

        crc32_table[n] = c;
    }
}


void get_fat_dir_time_date(uint16_t *in_time, uint16_t *in_date)
{
    time_t curr_time = time(NULL);
    struct tm tm = *localtime(&curr_time);

    *in_date =
        ((tm.tm_year - 80) << 9) |
        ((tm.tm_mon + 1) << 5) |
        tm.tm_mday;

    *in_time =
        (tm.tm_hour << 11) |
        (tm.tm_min << 5) |
        (tm.tm_sec / 2);
}


uint32_t calculate_crc32(void *buf, int32_t length)
{
    static bool made_crc_table = false;

    uint8_t *bufp = buf;
    uint32_t c = 0xFFFFFFFFL;
    int32_t n;

    if (!made_crc_table)
    {
        create_crc32_table();
        made_crc_table = true;
    }

    for (n = 0; n < length; n++)
    {
        c = crc32_table[(c ^ bufp[n]) & 0xFF] ^ (c >> 8);
    }

    return c ^ 0xFFFFFFFFL;
}


//Get FAT entry position
uint64_t fat_entry_offset(uint32_t fat_number, uint32_t cluster)
{
    return ((uint64_t)fat_start_lba +
            ((uint64_t)fat_number * fat_size_lbas)) * lba_size +
            ((uint64_t)cluster * sizeof(uint32_t));
}


//Write a FAT entry to all FATs
bool write_fat_entry(FILE *image, uint32_t cluster, uint32_t value)
{
    for (uint32_t i = 0; i < 2; i++)
    {
        uint64_t offset = fat_entry_offset(i, cluster);

        if (fseek(image, offset, SEEK_SET) != 0)
        {
            return false;
        }

        if (fwrite(&value, sizeof(value), 1, image) != 1)
        {
            return false;
        }
    }

    return true;
}


//Get the LBA for a FAT32 cluster
uint64_t cluster_to_lba(uint32_t cluster)
{
    //Cluster 2 is the first cluster in the data region
    return fat_data_lba + (cluster - 2);
}


//Allocate a FAT32 cluster
uint32_t allocate_cluster(FILE *image)
{
    uint32_t cluster = next_free_cluster++;

    if (!write_fat_entry(image, cluster, 0x0FFFFFFF))
    {
        return 0;
    }

    return cluster;
}


//Write data into a cluster
bool write_cluster(FILE *image, uint32_t cluster, const void *buffer, uint32_t size)
{
    if (size > lba_size)
    {
        return false;
    }

    uint64_t offset = cluster_to_lba(cluster) * lba_size;

    if (fseek(image, offset, SEEK_SET) != 0)
    {
        return false;
    }

    if (fwrite(buffer, 1, size, image) != size)
    {
        return false;
    }

    uint8_t zeros[512] = {0};

    if (size < lba_size)
    {
        fwrite(zeros, 1, lba_size - size, image);
    }

    return true;
}


//Create a short FAT32 directory entry
FAT32_Dir_Entry_Short create_dir_entry(
    const char name[11],
    uint8_t attr,
    uint32_t first_cluster,
    uint32_t file_size)
{
    FAT32_Dir_Entry_Short entry = {0};

    memcpy(entry.DIR_Name, name, 11);

    entry.DIR_Attr = attr;

    uint16_t time = 0;
    uint16_t date = 0;

    get_fat_dir_time_date(&time, &date);

    entry.DIR_CrtTime = time;
    entry.DIR_CrtDate = date;
    entry.DIR_WrtTime = time;
    entry.DIR_WrtDate = date;
    entry.DIR_LstAccDate = date;

    entry.DIR_FstClusHI = first_cluster >> 16;
    entry.DIR_FstClusLO = first_cluster & 0xFFFF;

    entry.DIR_FileSize = file_size;

    return entry;
}


//Write a directory entry into a directory cluster
bool write_directory_entry(
    FILE *image,
    uint32_t directory_cluster,
    uint32_t entry_number,
    FAT32_Dir_Entry_Short *entry)
{
    uint64_t offset =
        cluster_to_lba(directory_cluster) * lba_size +
        (entry_number * sizeof(FAT32_Dir_Entry_Short));

    if (fseek(image, offset, SEEK_SET) != 0)
    {
        return false;
    }

    if (fwrite(entry, sizeof(*entry), 1, image) != 1)
    {
        return false;
    }

    return true;
}


//Write a file into the ESP
bool write_file_to_esp(
    FILE *image,
    uint32_t parent_cluster,
    uint32_t entry_number,
    const char name[11],
    const uint8_t *file_data,
    uint32_t file_size)
{
    uint32_t first_cluster = allocate_cluster(image);

    if (first_cluster == 0)
    {
        return false;
    }

    uint32_t current_cluster = first_cluster;
    uint32_t bytes_written = 0;

    //Write file data one cluster at a time
    while (bytes_written < file_size)
    {
        uint32_t bytes_remaining = file_size - bytes_written;

        uint32_t bytes_this_cluster =
            bytes_remaining > lba_size ?
            lba_size :
            bytes_remaining;

        if (!write_cluster(
                image,
                current_cluster,
                file_data + bytes_written,
                bytes_this_cluster))
        {
            return false;
        }

        bytes_written += bytes_this_cluster;

        if (bytes_written < file_size)
        {
            uint32_t next_cluster = allocate_cluster(image);

            if (next_cluster == 0)
            {
                return false;
            }

            if (!write_fat_entry(image, current_cluster, next_cluster))
            {
                return false;
            }

            current_cluster = next_cluster;
        }
    }

    FAT32_Dir_Entry_Short entry =
        create_dir_entry(
            name,
            ATTR_ARCHIVE,
            first_cluster,
            file_size);

    return write_directory_entry(
        image,
        parent_cluster,
        entry_number,
        &entry);
}


//Get size of a file
uint32_t get_file_size(FILE *file)
{
    fseek(file, 0, SEEK_END);

    long size = ftell(file);

    fseek(file, 0, SEEK_SET);

    if (size < 0 || size > UINT32_MAX)
    {
        return 0;
    }

    return (uint32_t)size;
}


//Read a file and add it to the ESP
bool add_file_to_esp(
    FILE *image,
    uint32_t parent_cluster,
    uint32_t entry_number,
    const char name[11],
    const char *file_path)
{
    FILE *file = fopen(file_path, "rb");

    if (!file)
    {
        fprintf(stderr, "Error: couldn't open ESP file %s\n", file_path);
        return false;
    }

    uint32_t file_size = get_file_size(file);

    uint8_t *buffer = malloc(file_size);

    if (!buffer)
    {
        fclose(file);
        return false;
    }

    if (fread(buffer, 1, file_size, file) != file_size)
    {
        free(buffer);
        fclose(file);
        return false;
    }

    fclose(file);

    bool result =
        write_file_to_esp(
            image,
            parent_cluster,
            entry_number,
            name,
            buffer,
            file_size);

    free(buffer);

    return result;
}


//Write protective MBR for GPT
bool write_mbr(FILE *image)
{
    //Don't want to change global image_size_lbas
    uint64_t mbr_image_lbas = image_size_lbas;

    if (mbr_image_lbas > 0xFFFFFFFF)
    {
        mbr_image_lbas = 0xFFFFFFFF;
    }

    Mbr mbr = {
        .boot_code = {0},
        .mbr_signature = 0,
        .unknown = 0,

        .partition[0] = {
            .boot_indicator = 0,
            .starting_chs = {0x00, 0x02, 0x00}, //Doesn't matter for GPT
            .os_type = 0xEE, //This signals that this is a protective MBR for GPT
            .ending_chs = {0xFF, 0xFF, 0xFF}, //Dosen't matter for GPT
            .starting_lba = 0x00000001,
            .size_lba = (uint32_t)(mbr_image_lbas - 1),
        },

        .boot_signature = 0xAA55,
    };

    //Write to file
    if (fseek(image, 0, SEEK_SET) != 0)
    {
        return false;
    }

    if (fwrite(&mbr, 1, sizeof(mbr), image) != sizeof(mbr))
    {
        return false;
    }

    write_full_lba_size(image);

    return true;
}


//Writes the primary GPT header
bool write_gpts(FILE *image)
{
    Gpt_Header primary_gpt = {
        .signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
        .revision = 0x00010000,
        .header_size = 92,
        .header_crc32 = 0,
        .reserved_l = 0,
        .my_lba = 1,
        .alternate_lba = image_size_lbas - 1,
        .first_usable_lba = 2 + gpt_table_lbas,
        .last_usable_lba = image_size_lbas - 2 - gpt_table_lbas,
        .disk_guid = new_guid(),
        .partition_table_lba = 2,
        .number_of_entries = NUMBER_OF_GPT_TABLE_ENTRIES,
        .size_of_entry = GPT_TABLE_ENTRY_SIZE,
        .partition_table_crc32 = 0,
        .reserved_2 = {0},
    };


    // Fill out primary table with partitions
    //GPT header will refer to this same table, from two different places on disk
    Gpt_Partition_Entry gpt_table[NUMBER_OF_GPT_TABLE_ENTRIES] = {
        //EFI System Partition
        {
            .partition_type_guid = ESP_GUID,
            .unique_guid = new_guid(),
            .starting_lba = esp_lba,
            .ending_lba = esp_lba + esp_size_lbas - 1,
            .attributes = 0,
            .name = u"EFI SYSTEM",
        },

        //Basic Data Partition
        {
            .partition_type_guid = BASIC_DATA_GUID,
            .unique_guid = new_guid(),
            .starting_lba = data_lba,
            .ending_lba = data_lba + data_size_lbas - 1,
            .attributes = 0,
            .name = u"BASIC DATA",
        },
    };


    //Fill out primary header CRC32
    primary_gpt.partition_table_crc32 =
        calculate_crc32(gpt_table, sizeof(gpt_table));

    primary_gpt.header_crc32 =
        calculate_crc32(&primary_gpt, primary_gpt.header_size);


    //Write primary GPT header and table to file
    fseek(image, lba_size, SEEK_SET);

    if (fwrite(&primary_gpt, 1, sizeof(primary_gpt), image) != sizeof(primary_gpt))
    {
        return false;
    }

    write_full_lba_size(image);

    if (fwrite(gpt_table, 1, sizeof(gpt_table), image) != sizeof(gpt_table))
    {
        return false;
    }


    //Fill out secondary GPT header
    Gpt_Header secondary_gpt = primary_gpt;

    secondary_gpt.partition_table_lba =
        image_size_lbas - 1 - gpt_table_lbas;

    secondary_gpt.my_lba = image_size_lbas - 1;
    secondary_gpt.alternate_lba = primary_gpt.my_lba;

    secondary_gpt.header_crc32 = 0;

    secondary_gpt.header_crc32 =
        calculate_crc32(
            &secondary_gpt,
            secondary_gpt.header_size);


    //Go to positon of seconday table
    fseek(
        image,
        secondary_gpt.partition_table_lba * lba_size,
        SEEK_SET);


    //Write secondary gpt table to file
    if (fwrite(gpt_table, 1, sizeof(gpt_table), image) != sizeof(gpt_table))
    {
        return false;
    }


    //Write secondary gpt header to file
    fseek(
        image,
        secondary_gpt.my_lba * lba_size,
        SEEK_SET);

    if (fwrite(&secondary_gpt, 1, sizeof(secondary_gpt), image) != sizeof(secondary_gpt))
    {
        return false;
    }

    write_full_lba_size(image);

    return true;
}


//Write ESP system partition with FAT32 filesystem
bool write_esp(FILE *image)
{
    //Reserved sector region -------------------------------
    const uint16_t reserved_sectors = 32; //FAT32 spec says this should be at least 32 sectors, but can be more

    //FAT32 size calculation
    uint32_t sectors_per_cluster = 1;
    uint32_t fat_entries_needed =
        (uint32_t)(esp_size_lbas / sectors_per_cluster) + 2;

    uint32_t fat_bytes_needed =
        fat_entries_needed * sizeof(uint32_t);

    uint32_t fat_sectors_needed =
        (fat_bytes_needed + lba_size - 1) / lba_size;


    Fat32_Vbr vbr = {
        .BS_jmpBoot = {0xEB, 0x58, 0x90},
        .BS_OEMName = {'M', 'S', 'W', 'I', 'N', '4', '.', '1'},
        .BPB_BytsPerSec = lba_size,
        .BPB_SecPerClus = sectors_per_cluster,
        .BPB_RsvdSecCnt = reserved_sectors,
        .BPB_NumFATs = 2,
        .BPB_RootEntCnt = 0,
        .BPB_TotSec16 = 0,
        .BPB_Media = 0xF8,
        .BPB_FATSz16 = 0,
        .BPB_SecPerTrk = 0,
        .BPB_NumHeads = 0,
        .BPB_HiddSec = esp_lba,
        .BPB_TotSec32 = esp_size_lbas,
        .BPB_FATSz32 = fat_sectors_needed,
        .BPB_ExtFlags = 0,
        .BPB_FSVer = 0,
        .BPB_RootClus = 2,
        .BPB_FSInfo = 1,
        .BPB_BkBootSec = 6,
        .BPB_Reserved = {0},
        .BS_DrvNum = 0x80,
        .BS_Reserved1 = 0,
        .BS_BootSig = 0x29,
        .BS_VolID = 0,
        .BS_VolLab = {'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' '},
        .BS_FilSysType = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '},
        .boot_code = {0},
        .bootsect_sig = 0xAA55,
    };


    //Fill out file system info sector
    FSInfo fsinfo = {
        .FSI_LeadSig = 0x41615252,
        .FSI_Reserved1 = {0},
        .FSI_StructSig = 0x61417272,
        .FSI_Free_Count = 0xFFFFFFFF,
        .FSI_Nxt_Free = 5,
        .FSI_Reserved2 = {0},
        .FSI_TrailSig = 0xAA550000,
    };


    //Write VBR and FSInfo
    fseek(image, esp_lba * lba_size, SEEK_SET);

    if (fwrite(&vbr, 1, sizeof(vbr), image) != sizeof(vbr))
    {
        fprintf(stdout, "Error: VBR didn't write to img\n");
        return false;
    }

    write_full_lba_size(image);


    if (fwrite(&fsinfo, 1, sizeof(fsinfo), image) != sizeof(fsinfo))
    {
        fprintf(stdout, "Error: FSInfo didn't write to img\n");
        return false;
    }

    write_full_lba_size(image);


    //go to backup boot sector location
    fseek(
        image,
        (esp_lba + vbr.BPB_BkBootSec) * lba_size,
        SEEK_SET);

    if (fwrite(&vbr, 1, sizeof(vbr), image) != sizeof(vbr))
    {
        fprintf(stdout, "Error: VBR didn't write to img\n");
        return false;
    }

    write_full_lba_size(image);


    if (fwrite(&fsinfo, 1, sizeof(fsinfo), image) != sizeof(fsinfo))
    {
        fprintf(stdout, "Error: FSInfo didn't write to img\n");
        return false;
    }

    write_full_lba_size(image);


    //FAT region -------------------------------
    fat_start_lba = esp_lba + vbr.BPB_RsvdSecCnt;
    fat_size_lbas = vbr.BPB_FATSz32;

    fat_data_lba =
        fat_start_lba +
        (vbr.BPB_NumFATs * fat_size_lbas);


    //Write FATs
    for (uint32_t i = 0; i < vbr.BPB_NumFATs; i++)
    {
        uint64_t offset =
            ((uint64_t)fat_start_lba +
            ((uint64_t)i * fat_size_lbas)) * lba_size;

        fseek(image, offset, SEEK_SET);

        uint32_t cluster;

        //Cluster 0, FAT indicator, lowest 8 bits are media byte
        cluster = 0x0FFFFFF0 | vbr.BPB_Media;
        fwrite(&cluster, sizeof(cluster), 1, image);

        //Cluster 1
        cluster = 0x0FFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, image);

        //Cluster 2 Root dir cluster start
        cluster = 0x0FFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, image);

        //Cluster 3 '/EFI' dir cluster
        cluster = 0x0FFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, image);

        //Cluster 4 '/EFI/BOOT' dir cluster
        cluster = 0x0FFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, image);
    }


    //Data region -------------------------------
    //Write File data

    //Root '/' Dir
    FAT32_Dir_Entry_Short dir_ent;

    dir_ent =
        create_dir_entry(
            "EFI        ",
            ATTR_DIRECTORY,
            3,
            0);

    if (!write_directory_entry(image, 2, 0, &dir_ent))
    {
        return false;
    }


    //EFI Dir
    FAT32_Dir_Entry_Short dot =
        create_dir_entry(
            ".          ",
            ATTR_DIRECTORY,
            3,
            0);

    FAT32_Dir_Entry_Short dotdot =
        create_dir_entry(
            "..         ",
            ATTR_DIRECTORY,
            2,
            0);

    FAT32_Dir_Entry_Short boot =
        create_dir_entry(
            "BOOT       ",
            ATTR_DIRECTORY,
            4,
            0);


    if (!write_directory_entry(image, 3, 0, &dot))
    {
        return false;
    }

    if (!write_directory_entry(image, 3, 1, &dotdot))
    {
        return false;
    }

    if (!write_directory_entry(image, 3, 2, &boot))
    {
        return false;
    }


    //EFI/BOOT Dir
    dot =
        create_dir_entry(
            ".          ",
            ATTR_DIRECTORY,
            4,
            0);

    dotdot =
        create_dir_entry(
            "..         ",
            ATTR_DIRECTORY,
            3,
            0);


    if (!write_directory_entry(image, 4, 0, &dot))
    {
        return false;
    }

    if (!write_directory_entry(image, 4, 1, &dotdot))
    {
        return false;
    }


    //Create DSKIMG.INF file -------------------------------
    char dskimg_inf[128];

    int dskimg_inf_size =
        snprintf(
            dskimg_inf,
            sizeof(dskimg_inf),
            "DISK_SIZE=%llu\n",
            (unsigned long long)image_size);

    if (dskimg_inf_size < 0 ||
        dskimg_inf_size >= (int)sizeof(dskimg_inf))
    {
        fprintf(stderr, "Error: couldn't create DSKIMG.INF\n");
        return false;
    }


    //Add DSKIMG.INF to /EFI/BOOT/
    if (!write_file_to_esp(
            image,
            4,
            2,
            "DSKIMG  INF",
            (const uint8_t *)dskimg_inf,
            (uint32_t)dskimg_inf_size))
    {
        fprintf(stderr, "Error: couldn't write DSKIMG.INF to ESP\n");
        return false;
    }


    //Add BOOTX64.EFI to /EFI/BOOT/ ------------------------
    //Use the --bootloader path if one was given, otherwise fall back to
    //looking for BOOTX64.EFI in the current directory.
    const char *bootloader_source = bootloader_path ? bootloader_path : "BOOTX64.EFI";

    FILE *boot_test = fopen(bootloader_source, "rb");

    if (boot_test)
    {
        fclose(boot_test);

        if (!add_file_to_esp(
                image,
                4,
                3,
                "BOOTX64 EFI",
                bootloader_source))
        {
            fprintf(stderr, "Error: couldn't write BOOTX64.EFI to ESP\n");
            return false;
        }
    }
    else
    {
        fprintf(
            stdout,
            "Warning: %s not found, skipping EFI boot loader\n",
            bootloader_source);
    }


    return true;
}


//Print program usage/help text
void print_usage(const char *prog_name)
{
    fprintf(stdout,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -i,  --image <file>       Output disk image\n"
        "  -es, --esp-size <size>    EFI System Partition size\n"
        "  -ds, --data-size <size>   Basic Data partition size\n"
        "  -b,  --bootloader <file>  UEFI bootloader to add\n"
        "  -l,  --lba-size <bytes>   Logical block size\n"
        "  -v,  --verbose             Print image layout\n"
        "  -r,  --run                 Launch QEMU under OVMF after building\n"
        "       --ovmf-code <file>   Path to OVMF_CODE (required with -r)\n"
        "       --ovmf-vars <file>   Path to a writable OVMF_VARS copy (required with -r)\n"
        "  -h,  --help                Show this help message\n",
        prog_name);
}


//Parse a size string like "33M", "1G", "512K", or a plain byte count
//Supported (case-insensitive) suffixes: K/KB, M/MB, G/GB. No suffix = bytes.
//Returns 0 and sets *ok = false on a malformed value.
uint64_t parse_size(const char *str, bool *ok)
{
    *ok = true;

    if (!str || !*str)
    {
        *ok = false;
        return 0;
    }

    char *end = NULL;
    double value = strtod(str, &end);

    if (end == str || value < 0)
    {
        *ok = false;
        return 0;
    }

    uint64_t multiplier = 1;

    if (*end != '\0')
    {
        if (strcasecmp(end, "k") == 0 || strcasecmp(end, "kb") == 0)
        {
            multiplier = 1024ULL;
        }
        else if (strcasecmp(end, "m") == 0 || strcasecmp(end, "mb") == 0)
        {
            multiplier = 1024ULL * 1024ULL;
        }
        else if (strcasecmp(end, "g") == 0 || strcasecmp(end, "gb") == 0)
        {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        }
        else
        {
            *ok = false;
            return 0;
        }
    }

    return (uint64_t)(value * (double)multiplier);
}


//Parse command-line arguments, filling in the global config.
//Returns false (and prints usage) on a bad/missing argument. If -h/--help
//is given, prints usage and exits the program directly (success).
bool parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        bool is_image      = strcmp(arg, "-i")  == 0 || strcmp(arg, "--image")      == 0;
        bool is_esp_size    = strcmp(arg, "-es") == 0 || strcmp(arg, "--esp-size")   == 0;
        bool is_data_size   = strcmp(arg, "-ds") == 0 || strcmp(arg, "--data-size")  == 0;
        bool is_bootloader  = strcmp(arg, "-b")  == 0 || strcmp(arg, "--bootloader") == 0;
        bool is_lba_size    = strcmp(arg, "-l")  == 0 || strcmp(arg, "--lba-size")   == 0;
        bool is_verbose     = strcmp(arg, "-v")  == 0 || strcmp(arg, "--verbose")    == 0;
        bool is_run         = strcmp(arg, "-r")  == 0 || strcmp(arg, "--run")        == 0;
        bool is_ovmf_code   = strcmp(arg, "--ovmf-code") == 0;
        bool is_ovmf_vars   = strcmp(arg, "--ovmf-vars") == 0;
        bool is_help        = strcmp(arg, "-h")  == 0 || strcmp(arg, "--help")       == 0;

        if (is_help)
        {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else if (is_verbose)
        {
            verbose_flag = true;
        }
        else if (is_run)
        {
            run_flag = true;
        }
        else if (is_image || is_esp_size || is_data_size || is_bootloader ||
                 is_lba_size || is_ovmf_code || is_ovmf_vars)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                print_usage(argv[0]);
                return false;
            }

            const char *value = argv[++i];

            if (is_image)
            {
                image_name = (char *)value;
            }
            else if (is_bootloader)
            {
                bootloader_path = (char *)value;
            }
            else if (is_ovmf_code)
            {
                ovmf_code_path = (char *)value;
            }
            else if (is_ovmf_vars)
            {
                ovmf_vars_path = (char *)value;
            }
            else
            {
                bool ok = false;
                uint64_t parsed = parse_size(value, &ok);

                if (!ok)
                {
                    fprintf(stderr, "Error: invalid size '%s' for %s\n", value, arg);
                    print_usage(argv[0]);
                    return false;
                }

                if (is_esp_size)
                {
                    esp_size = parsed;
                }
                else if (is_data_size)
                {
                    data_size = parsed;
                }
                else if (is_lba_size)
                {
                    lba_size = parsed;
                }
            }
        }
        else
        {
            fprintf(stderr, "Error: unrecognized option '%s'\n", arg);
            print_usage(argv[0]);
            return false;
        }
    }

    if (run_flag && (!ovmf_code_path || !ovmf_vars_path))
    {
        fprintf(stderr,
            "Error: --run requires both --ovmf-code and --ovmf-vars to be given\n");
        print_usage(argv[0]);
        return false;
    }

    return true;
}


//Check whether a file exists (and is at least readable)
bool file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}


//Launch qemu-system-x86_64 with the given image under the given OVMF
//firmware. Returns true if QEMU launched and exited with status 0.
bool run_in_qemu(void)
{
    if (!file_exists(ovmf_code_path))
    {
        fprintf(stderr, "Error: OVMF code file not found: %s\n", ovmf_code_path);
        return false;
    }

    if (!file_exists(ovmf_vars_path))
    {
        fprintf(stderr,
            "Error: OVMF vars file not found: %s\n"
            "  This must be a writable copy, not the read-only system template.\n"
            "  e.g. cp /usr/share/OVMF/OVMF_VARS_4M.fd %s\n",
            ovmf_vars_path, ovmf_vars_path);
        return false;
    }

    char image_arg[512];
    char code_arg[512];
    char vars_arg[512];

    snprintf(image_arg, sizeof(image_arg), "file=%s,format=raw", image_name);
    snprintf(code_arg, sizeof(code_arg),
        "if=pflash,format=raw,readonly=on,file=%s", ovmf_code_path);
    snprintf(vars_arg, sizeof(vars_arg),
        "if=pflash,format=raw,file=%s", ovmf_vars_path);

    char *qemu_argv[] = {
        "qemu-system-x86_64",
        "-drive", image_arg,
        "-drive", code_arg,
        "-drive", vars_arg,
        NULL
    };

    fprintf(stdout, "Launching QEMU: %s -drive %s -drive %s -drive %s\n",
        qemu_argv[0], image_arg, code_arg, vars_arg);

    pid_t pid = fork();

    if (pid < 0)
    {
        fprintf(stderr, "Error: fork() failed, couldn't launch QEMU\n");
        return false;
    }

    if (pid == 0)
    {
        //Child process: replace with QEMU
        execvp("qemu-system-x86_64", qemu_argv);

        //execvp only returns on failure
        fprintf(stderr, "Error: couldn't run qemu-system-x86_64 (is it installed and on PATH?)\n");
        _exit(EXIT_FAILURE);
    }

    //Parent process: wait for QEMU to exit
    int status = 0;

    if (waitpid(pid, &status, 0) < 0)
    {
        fprintf(stderr, "Error: waitpid() failed\n");
        return false;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}


//Print a summary of the final on-disk layout (used by --verbose)
void print_layout(void)
{
    fprintf(stdout,
        "Image layout:\n"
        "  Output file:        %s\n"
        "  LBA size:            %llu bytes\n"
        "  Image size:          %llu bytes (%llu LBAs)\n"
        "  Protective MBR:      LBA 0\n"
        "  Primary GPT header:  LBA 1\n"
        "  Primary GPT table:   LBA 2 - %llu\n"
        "  ESP:                 LBA %llu - %llu (%llu bytes)\n"
        "  Basic Data partition:LBA %llu - %llu (%llu bytes)\n"
        "  Backup GPT table:    LBA %llu - %llu\n"
        "  Backup GPT header:   LBA %llu\n"
        "  Bootloader source:   %s\n",
        image_name,
        (unsigned long long)lba_size,
        (unsigned long long)image_size,
        (unsigned long long)image_size_lbas,
        (unsigned long long)(1 + gpt_table_lbas),
        (unsigned long long)esp_lba,
        (unsigned long long)(esp_lba + esp_size_lbas - 1),
        (unsigned long long)esp_size,
        (unsigned long long)data_lba,
        (unsigned long long)(data_lba + data_size_lbas - 1),
        (unsigned long long)data_size,
        (unsigned long long)(image_size_lbas - 1 - gpt_table_lbas),
        (unsigned long long)(image_size_lbas - 2),
        (unsigned long long)(image_size_lbas - 1),
        bootloader_path ? bootloader_path : "BOOTX64.EFI (default, cwd)");
}


int main(int argc, char *argv[])
{
    if (!parse_args(argc, argv))
    {
        return EXIT_FAILURE;
    }

    FILE *image = fopen(image_name, "wb+");

    if (!image)
    {
        fprintf(
            stderr,
            "Error: file couldn't be opened %s\n",
            image_name);

        return EXIT_FAILURE;
    }


    //Set sizes
    align_lba = ALIGNMENT / lba_size;

    esp_size_lbas = bytes_to_lbas(esp_size);
    data_size_lbas = bytes_to_lbas(data_size);

    gpt_table_lbas = GPT_TABLE_SIZE / lba_size;


    //ESP starts after alignment
    esp_lba = align_lba;


    //Data partition starts after ESP
    data_lba =
        next_aligned_lba(
            esp_lba + esp_size_lbas);


    //Calculate enough space for both partitions and backup GPT
    uint64_t last_partition_lba =
        data_lba + data_size_lbas;


    //Backup GPT table and header
    image_size_lbas =
        last_partition_lba +
        gpt_table_lbas +
        1;


    //Make sure image size has enough space
    image_size =
        image_size_lbas * lba_size;


    //Seed rand()
    srand(time(NULL));


    //Extend image to its final size
    if (fseek(image, image_size - 1, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: couldn't seek to end of image\n");
        fclose(image);
        return EXIT_FAILURE;
    }

    if (fputc(0, image) == EOF)
    {
        fprintf(stderr, "Error: couldn't allocate image size\n");
        fclose(image);
        return EXIT_FAILURE;
    }


    if (!write_mbr(image))
    {
        fprintf(
            stderr,
            "Error: could not write protective MBR for file %s\n",
            image_name);

        fclose(image);
        return EXIT_FAILURE;
    }


    //Write GPT headers & tables
    if (!write_gpts(image))
    {
        fprintf(
            stderr,
            "Error: could not write GPT headers and tables for file %s\n",
            image_name);

        fclose(image);
        return EXIT_FAILURE;
    }


    //Write ESP system partiton with FAT32 filesystem
    if (!write_esp(image))
    {
        fprintf(
            stderr,
            "Error: could not write ESP for file %s\n",
            image_name);

        fclose(image);
        return EXIT_FAILURE;
    }


    fclose(image);

    if (verbose_flag)
    {
        print_layout();
    }

    if (run_flag)
    {
        if (!run_in_qemu())
        {
            fprintf(stderr, "Error: QEMU run failed\n");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}