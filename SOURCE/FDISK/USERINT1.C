#define USERINTM

#include <conio.h>
#include <ctype.h>
#ifndef __WATCOMC__
#include <dir.h>
#endif
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "fdiskio.h"
#include "kbdinput.h"
#include "main.h"
#include "pcompute.h"
#include "pdiskio.h"
#include "userint1.h"
#include "userint2.h"

/* Clear Screen */
void Clear_Screen( int type ) /* Clear screen code as suggested by     */
{                             /* Ralf Quint                            */
   if ( flags.monochrome == TRUE ) {
      Clear_Screen_With_Attr( type, 0x07 );
   }
   else {
      Clear_Screen_With_Attr( type, flags.screen_color );
   }
}

void Clear_Screen_With_Attr( int type, unsigned char attr )
{
   asm {
    mov ah, 0x0f /* get max column to clear */
    int 0x10
    mov dh, ah

    mov ah, 0x06 /* scroll up */
    mov al, 0x00 /* 0 rows, clear whole window */
    mov bh, BYTE PTR attr /* set color */
    xor cx, cx      /* coordinates of upper left corner of screen */
         /*    mov dh,25    */ /* maximum row */
    mov dl, 79 /* maximum column */
    push bp /* work arount IBM-XT BIOS bug */
    int 0x10
    pop bp
   }

   if ( type != NOEXTRAS )
   {
      Display_Information();
      /*Display_Label();*/
   }
}

/* Display Information */
void Display_Information( void )
{
   if ( flags.extended_options_flag == TRUE ) {
      Position_Cursor( 0, 0 );
      if ( flags.version == FOUR ) {
         Color_Print( "4" );
      }
      if ( flags.version == FIVE ) {
         Color_Print( "5" );
      }
      if ( flags.version == SIX ) {
         Color_Print( "6" );
      }
      if ( flags.version == W95 ) {
         Color_Print( "W95" );
      }
      if ( flags.version == W95B ) {
         Color_Print( "W95B" );
      }
      if ( flags.version == W98 ) {
         Color_Print( "W98" );
      }

      if ( flags.partition_type_lookup_table == INTERNAL ) {
         Color_Print_At( 5, 0, "INT" );
      }
      else {
         Color_Print_At( 5, 0, "EXT" );
      }

      if ( flags.use_extended_int_13 == TRUE ) {
         Color_Print_At( 9, 0, "LBA" );
      }

      if ( flags.fat32 == TRUE ) {
         Color_Print_At( 13, 0, "FAT32" );
      }

      if ( flags.use_ambr == TRUE ) {
         Color_Print_At( 72, 0, "AMBR" );
      }

      if ( flags.partitions_have_changed == TRUE ) {
         Color_Print_At( 77, 0, "C" );
      }

      if ( flags.extended_options_flag == TRUE ) {
         Color_Print_At( 79, 0, "X" );
      }
   }

#ifndef RELEASE
   Position_Cursor( 0, flags.extended_options_flag ? 1 : 0 );
   Color_Print( "NON-RELEASE BUILD" );
   Position_Cursor( 60, flags.extended_options_flag ? 1 : 0 );
   Color_Print( __DATE__ " " __TIME__ );
#endif

#ifdef DEBUG
   Color_Print_At( 60, 0, "DEBUG" );

   if ( debug.emulate_disk > 0 ) {
      Color_Print_At( 66, 0, "E%1d", debug.emulate_disk );
   }

   if ( debug.write == FALSE ) {
      Color_Print_At( 69, 0, "RO" );
   }
#endif
}

/* Display Label */
/* unused
void Display_Label( void )
{
   if ( flags.label == TRUE ) {
      int index = 0;

      char label[20];

      strcpy( label, FD_NAME );

      do {
         Print_At( 79, ( ( index * 2 ) + 3 ), "%c", label[index] );
         index++;
      } while ( index < 10 );
   }
}
*/
/* Exit Screen */
void Exit_Screen( void )
{
   if ( flags.partitions_have_changed == TRUE ) {
      Write_Partition_Tables();
      flags.partitions_have_changed = FALSE;

      Clear_Screen( NOEXTRAS );

      if ( flags.reboot == FALSE ) {
         Print_At( 4, 11, "You " );
         Color_Print( "MUST" );
         printf( " restart your system for your changes to take effect." );
         Print_At(
            4, 12,
            "Any drives you have created or changed must be formatted" );
         Color_Print_At( 4, 13, "AFTER" );
         printf( " you restart." );

         Input( 0, 0, 0, ESC, 0, 0, ESCE, 0, 0, '\0', '\0' );
         Clear_Screen( NOEXTRAS );
      }
      else {
         Color_Print_At( 4, 13, "System will now restart" );
         Print_At( 4, 15, "Press any key when ready . . ." );

         /* Wait for a keypress. */
         asm {
        mov ah,7
        int 0x21
         }

         Reboot_PC();
      }
   }
   else {
      Clear_Screen( NOEXTRAS );
   }
}

void Warn_Incompatible_Ext( void )
{
   Clear_Screen( NOEXTRAS );

   Color_Print_At( 38, 4, "ERROR" );

   Position_Cursor( 0, 7 );
   printf(
      "    A non-compatible extended partition layout was detected on\n"
      "    this disk. The following actions are disabled:\n\n"
      "      - creating logical drives\n"
      "      - deleting logical drives\n\n"
      "    You may re-create the extended partition to enable editing or\n"
      "    use another disk utility to partition this disk.\n" );

   Input( 0, 0, 0, ESC, 0, 0, ESCR, 0, 0, '\0', '\0' );
}

/* Interactive User Interface Control Routine */
void Interactive_User_Interface( void )
{
   int counter = 0;
   int index = 0;
   int menu = MM;
   Partition_Table *pDrive = &part_table[flags.drive_number - 0x80];

   flags.verbose = flags.quiet = 0;

   /* abort if user decides so after beeing informed of FDISK not able
      to correctly handle disks too large */
   for ( index = 0; index <= flags.maximum_drive_number - 0x80; ++index ) {
      if ( part_table[index].size_truncated ) {
         if ( !Inform_About_Trimmed_Disk() ) {
            goto ret;
         }
      }
   }

   /* Ask the user if FAT32 is desired. */
   if ( ( flags.version == W95B ) || ( flags.version == W98 ) ) {
      Ask_User_About_FAT32_Support();
   }

   //  Create_MBR_If_Not_Present();     DO NOT AUTOMATICALLY CREATE THE MBR.
   //                                   THIS FEATURE WAS REQUESTED TO BE
   //                                   DISABLED.

   do {

      menu = Standard_Menu( menu );

      pDrive = &part_table[flags.drive_number - 0x80];

      /* Definitions for the menus */
      /* MM   0x00                  Main Menu                     */

      /*   CP   0x10                Create PDP or LDD             */

      /*     CPDP 0x11              Create Primary DOS Partition  */
      /*     CEDP 0x12              Create Extended DOS Partition */
      /*     CLDD 0x13              Create Logical DOS Drive      */

      /*   SAP  0x20                Set Active Partition          */

      /*   DP   0x30                Delete partition or LDD       */

      /*     DPDP 0x31              Delete Primary DOS Partition  */
      /*     DEDP 0x32              Delete Extended DOS Partition */
      /*     DLDD 0x33              Delete Logical DOS Drive      */
      /*     DNDP 0x34              Delete Non-DOS Partition      */

      /*   DPI  0x40                Display Partition Information */

      /*   CD   0x50                Change Drive                  */

      /*   MBR  0x60                MBR Functions                 */

      /*     BMBR 0x61              Write booteasy MBR to drive   */
      /*     AMBR 0x62              Write alternate MBR to drive  */
      /*     SMBR 0x63              Save MBR to file              */
      /*     RMBR 0x64              Remove MBR from disk          */

      /* EXIT 0x0f                  Code to Exit from Program     */

      if ( ( menu == CPDP ) || ( menu == CEDP ) ) {
         /* Ensure that space is available in the primary partition table */
         /* to create a partition.                                        */

         /* First make sure that an empty slot is available.  */
         index = 0;
         counter = 0;
         do {
            if ( pDrive->pri_part[index].num_type > 0 ) {
               counter++;
            }
            index++;
         } while ( index < 4 );

         /* Next, make sure that there is a space available of at least   */
         /* two cylinders.                                                */
         Determine_Free_Space();
         if ( pDrive->pri_free_space < 2 ) {
            counter = 4;
         }

         if ( counter > 3 ) {
            Clear_Screen( 0 );

            if ( menu == CPDP ) {
               Print_Centered( 4, "Create Primary DOS Partition", BOLD );
            }
            else {
               Print_Centered( 4, "Create Extended DOS Partition", BOLD );
            }

            Print_At( 4, 6, "Current fixed disk drive: " );
            Color_Printf( "%d", ( flags.drive_number - 127 ) );

            Display_Primary_Partition_Information_SS();

            Color_Print_At( 4, 22, "No space to create a DOS partition." );

            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
         }
      }

      if ( menu == CPDP ) {
         Create_DOS_Partition_Interface( PRIMARY );
      }
      if ( menu == CEDP ) {
         if ( Num_Ext_Part( pDrive ) > 0 ) {
            Clear_Screen( 0 );

            Print_Centered( 4, "Create Extended DOS Partition", BOLD );
            Print_At( 4, 6, "Current fixed disk drive: " );
            Color_Printf( "%d", ( flags.drive_number - 127 ) );

            Display_Primary_Partition_Information_SS();

            Color_Print_At( 4, 22, "Extended DOS Partition already exists." );

            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
         }
         else {
            Create_DOS_Partition_Interface( EXTENDED );
         }
      }

      if ( menu == CLDD ) {
         if ( pDrive->ptr_ext_part == NULL ) {
            Color_Print_At( 4, 22,
                            "Cannot create Logical DOS Drive without" );
            Color_Print_At(
               4, 23, "an Extended DOS Partition on the current drive." );
            Print_At( 4, 24, "                                        " );
            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
         }
         else {
            Create_Logical_Drive_Interface();
         }
      }

      if ( menu == SAP ) {
         Set_Active_Partition_Interface();
      }

      if ( menu == DPDP ) {
         /* Ensure that primary partitions are available to delete. */
         counter = 0;
         index = 0;

         do {
            if ( IsRecognizedFatPartition(
                    pDrive->pri_part[index].num_type ) ) {
               counter++;
            }

            index++;
         } while ( index < 4 );

         if ( counter == 0 ) {
            Color_Print_At( 4, 22, "No Primary DOS Partition to delete." );
            Print_At( 4, 24, "                                        " );
            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
         }
         /* */
         else {
            Delete_Primary_DOS_Partition_Interface();
         }
      }

      if ( menu == DEDP ) {
         if ( pDrive->ptr_ext_part == NULL ) {
            Color_Print_At( 4, 22, "No Extended DOS Partition to delete." );
            Print_At( 4, 24, "                                        " );
            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
         }
         else {
            Delete_Extended_DOS_Partition_Interface();
         }
      }

      if ( menu == DLDD ) {
         if ( ( pDrive->num_of_log_drives == 0 ) ||
              ( pDrive->ptr_ext_part == NULL ) ) {
            Color_Print_At( 4, 22, "No Logical DOS Drive(s) to delete." );
            Print_At( 4, 24, "                                        " );
            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
         }
         else {
            Delete_Logical_Drive_Interface();
         }
      }

      if ( menu == DNDP ) {
         /* First Ensure that Non-DOS partitions are available to delete. */
         index = 0;
         counter = 0;

         do {
            counter++;
            if ( IsRecognizedFatPartition(
                    pDrive->pri_part[index].num_type ) ) {
               counter--;
            }
            index++;
         } while ( index < 4 );

         if ( counter == 0 ) {
            Color_Print_At( 4, 22, "No Non-DOS Partition to delete." );
            Print_At( 4, 24, "                                        " );
            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
         }
         else {
            Delete_N_DOS_Partition_Interface();
         }
      }
      if ( menu == DPI ) {
         Display_Partition_Information();
      }

      if ( menu == CD ) {
         Change_Current_Fixed_Disk_Drive();
      }

      if ( menu == BMBR ) {
         /*         Create_BootEasy_MBR();
         Color_Print_At( 4, 22, "BootEasy MBR has been created." );
         Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );*/
      }

      if ( menu == AMBR ) {
         char home_path[255];
         FILE *file_pointer;

         strcpy( home_path, path );
         strcat( home_path, "boot.mbr" );
         /* Search the directory Free FDISK resides in before searching the */
         /* PATH in the environment for the boot.mbr file.                  */
         file_pointer = fopen( home_path, "rb" );

         if ( !file_pointer ) {
            file_pointer = fopen( searchpath( "boot.mbr" ), "rb" );
         }

         if ( !file_pointer ) {
            Color_Print_At(
               4, 22,
               "\nUnable to find the \"boot.mbr\" file...MBR has not been loaded.\n" );
         }
         else {
            Load_MBR( 0 );
            Color_Print_At( 4, 22,
                            "MBR has been written using \"boot.mbr\"" );
            Read_Partition_Tables();
         }
         Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
      }

      if ( menu == SMBR ) {
         Save_MBR();
         Color_Print_At( 4, 22, "MBR has been saved to \"boot.mbr\"" );
         Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
      }

      if ( menu == RMBR ) {
         Remove_IPL();
         Color_Print_At( 4, 22, "Boot code has been removed from MBR." );
         Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
      }

      if ( menu != EXIT ) {
         if ( ( menu > 0x0f ) || ( menu == MM ) ) {
            menu = MM;
         }
         else {
            menu = menu & 0xf0;
         }
      }

   } while ( menu != EXIT );

   if ( flags.return_from_iui == FALSE ) {
      Exit_Screen();
   }

ret:
   /* clear screen with "normal" black background and position cursor at the
      top left */
   Clear_Screen_With_Attr( NOEXTRAS, TEXT_ATTR_NORMAL );
   Position_Cursor( 0, 0 );
}

/* Pause Routine */
void Pause( void )
{
   printf( "\nPress any key to continue" );

   asm {
    mov ah,7
    int 0x21
   }
   printf( "\r                          \r" );
}

/* Position cursor on the screen */
void Position_Cursor( int column, int row )
{
   asm {
      /* Get video page number */
    mov ah,0x0f
    int 0x10

      /* Position Cursor */
    mov ah,0x02
    mov dh,byte ptr row
    mov dl,byte ptr column
    int 0x10
   }
}

/* Print Centered Text */
void Print_Centered( int y, char *text, int style )
{
   int x = 40 - strlen( text ) / 2;

   Position_Cursor( x, y );

   if ( style == BOLD ) {
      Color_Print( text );
   }
   else {
      printf( text );
   }
}

/* Print 7 Digit Unsigned Long Values */
void Print_UL( unsigned long number ) { printf( "%7lu", number ); }

/* Print 7 Digit Unsigned Long Values in bold print */
void Print_UL_B( unsigned long number ) { Color_Printf( "%7lu", number ); }

/* Standard Menu Routine */
/* Displays the menus laid out in a standard format and returns the */
/* selection chosen by the user.                                    */
int Standard_Menu( int menu )
{
   int counter;
   int index;

   int input;

   int minimum_option;
   int maximum_number_of_options = 0;

   char copyleft[80] = "";
   char program_name[60] = "";
   char program_description[60] = "";
   char version[30] = "";

   char title[60] = "";

   char option_1[60] = "";
   char option_2[60] = "";
   char option_3[60] = "";
   char option_4[60] = "";
   char option_5[60] = "Change current fixed disk drive";

   char optional_char_1 = '\0';
   char optional_char_2 = '\0';

   for ( ;; ) {
      Partition_Table *pDrive = &part_table[flags.drive_number - 0x80];
      minimum_option = 1;

      strcpy( program_name, FD_NAME );
      strcat( program_name, " V" );
      strcat( program_name, VERSION );

      strcpy( program_description, "Fixed Disk Setup Program" );
      strcpy( copyleft, "GNU GPL (c) " COPYLEFT " by Brian E. Reifsnyder"
                        " and The FreeDOS Community" );

      if ( menu == MM ) {
         maximum_number_of_options = 4;
         strcpy( title, "FDISK Options" );
         strcpy( option_1, "Create DOS partition or Logical DOS Drive" );
         strcpy( option_2, "Set Active partition" );
         strcpy( option_3, "Delete partition or Logical DOS Drive" );

         if ( flags.extended_options_flag == FALSE ) {
            strcpy( option_4, "Display partition information" );
         }
         else {
            strcpy( option_4, "Display/Modify partition information" );
         }
      }

      if ( menu == CP ) {
         maximum_number_of_options = 3;
         strcpy( title, "Create DOS Partition or Logical DOS Drive" );
         strcpy( option_1, "Create Primary DOS Partition" );
         strcpy( option_2, "Create Extended DOS Partition" );
         strcpy(
            option_3,
            "Create Logical DOS Drive(s) in the Extended DOS Partition" );
         strcpy( option_4, "" );
      }

      if ( menu == DP ) {
         maximum_number_of_options = 4;
         strcpy( title, "Delete DOS Partition or Logical DOS Drive" );
         strcpy( option_1, "Delete Primary DOS Partition" );
         strcpy( option_2, "Delete Extended DOS Partition" );
         strcpy(
            option_3,
            "Delete Logical DOS Drive(s) in the Extended DOS Partition" );
         strcpy( option_4, "Delete Non-DOS Partition" );
         if ( flags.version == FOUR ) {
            maximum_number_of_options = 3;
         }
      }

      if ( menu == MBR ) {
         maximum_number_of_options = 4;
         strcpy( title, "MBR Maintenance" );
         strcpy( option_1, "Create BootEasy MBR (disabled)" );
         strcpy( option_2, "Load MBR (partitions and code) from saved file" );
         strcpy( option_3, "Save the MBR (partitions and code) to a file" );
         strcpy( option_4, "Remove boot code from the MBR" );
      }

      /* Display Program Name and Copyright Information */
      Clear_Screen( 0 );

      if ( ( flags.extended_options_flag == TRUE ) && ( menu == MM ) ) {
         /* */
         flags.display_name_description_copyright = TRUE;
      }

      if ( flags.display_name_description_copyright == TRUE ) {
         Print_Centered( 0, program_name, STANDARD );
         Print_Centered( 1, program_description, STANDARD );
         Print_Centered( 2, copyleft, STANDARD );

         if ( flags.use_freedos_label == TRUE ) {
            strcpy( version, "Version:  " );
            strcat( version, VERSION );
            Position_Cursor( ( 76 - strlen( version ) ), 24 );
            printf( "%s", version );
         }
      }

      flags.display_name_description_copyright = FALSE;

      /* Display Menu Title(s) */
      Print_Centered( 4, title, BOLD );

      /* Display Current Drive Number */
      Print_At( 4, 6, "Current fixed disk drive: " );
      Color_Printf( "%d", ( flags.drive_number - 127 ) );

      if ( part_table[flags.drive_number - 128].usable ) {
         Color_Printf( "   %lu",
                       part_table[flags.drive_number - 128].disk_size_mb );
         printf( " Mbytes" );
      }
      else {
         Color_Print( " is unusable!" );
         minimum_option = 5;
      }

      if ( menu == DP ) {
         /* Ensure that primary partitions are available to delete. */
         counter = 0;
         index = 0;

         do {
            if ( pDrive->pri_part[index].num_type > 0 ) {
               counter++;
            }
            index++;
         } while ( index < 4 );

         if ( counter == 0 ) {
            Color_Print_At( 4, 22, "No partitions to delete." );
            Print_At( 4, 24, "                                        " );
            Input( 0, 0, 0, ESC, 0, 0, ESCC, 0, 0, '\0', '\0' );
            menu = MM;
            return ( menu );
         }
      }

      /* Display Menu */
      Print_At( 4, 8, "Choose one of the following:" );

      if ( minimum_option <= 1 ) {
         Color_Print_At( 4, 10, "1.  " );
         printf( "%s", option_1 );
      }
      if ( maximum_number_of_options > 1 && minimum_option <= 2 ) {
         Color_Print_At( 4, 11, "2.  " );
         printf( "%s", option_2 );
      }

      if ( maximum_number_of_options > 2 && minimum_option <= 3 ) {
         Color_Print_At( 4, 12, "3.  " );
         printf( "%s", option_3 );
      }

      if ( maximum_number_of_options > 3 && minimum_option <= 4 ) {
         Color_Print_At( 4, 13, "4.  " );
         printf( "%s", option_4 );
      }

      if ( ( menu == MM ) && ( flags.more_than_one_drive == TRUE ) ) {
         maximum_number_of_options = 5;
         Color_Print_At( 4, 14, "5.  " );
         printf( "%s", option_5 );
      }

      if ( menu == MM && flags.extended_options_flag == TRUE &&
           minimum_option == 1 ) {
         Color_Print_At( 50, 15, "M.  " );
         printf( "MBR maintenance" );

         optional_char_1 = 'M';
      }
      else {
         optional_char_1 = '\0';
      }

      if ( menu == MM && flags.allow_abort == TRUE && minimum_option == 1 ) {
         Color_Print_At( 50, 16, "A.  " );
         printf( "Abort changes and exit" );

         optional_char_2 = 'A';
      }
      else {
         optional_char_2 = '\0';
      }

      /* Display Special Messages */

      /* If there is not an active partition */
      if ( ( ( pDrive->pri_part[0].num_type > 0 ) ||
             ( pDrive->pri_part[1].num_type > 0 ) ||
             ( pDrive->pri_part[2].num_type > 0 ) ||
             ( pDrive->pri_part[3].num_type > 0 ) ) &&
           ( flags.drive_number == 0x80 ) && ( menu == MM ) &&
           ( pDrive->pri_part[0].active_status == 0 ) &&
           ( pDrive->pri_part[1].active_status == 0 ) &&
           ( pDrive->pri_part[2].active_status == 0 ) &&
           ( pDrive->pri_part[3].active_status == 0 ) ) {
         Color_Print_At( 4, 21, "WARNING! " );
         printf(
            "No partitions are set active - disk 1 is not startable unless" );
         Print_At( 4, 22, "a partition is set active" );
      }

      /* Get input from user */
      Print_At( 4, 17, "Enter choice: " );

      if ( menu == MM ) {
         input = (int)Input( 1, 19, 17, NUM, minimum_option,
                             maximum_number_of_options, ESCE, 1, 0,
                             optional_char_1, optional_char_2 );
      }
      else {
         input = (int)Input( 1, 19, 17, NUM, 1, maximum_number_of_options,
                             ESCR, -1, 0, '\0', '\0' );
      }

      /* Process the input */
      if ( input == 'A' ) {
         /* Abort any changes and exit the program immediately. */
         flags.screen_color = 7; /* Set screen colors back to default. */
         Clear_Screen( NOEXTRAS );
         exit( 0 );
      }

      if ( input == 'M' ) {
         input = 6;
      }

      if ( input != 0 ) {
         if ( menu == MM ) {
            menu = input << 4;
         }
         else {
            menu = menu | input;
         }
      }
      else {
         if ( menu == MM ) {
            menu = EXIT;
         }
         else {
            if ( menu > 0x0f ) {
               menu = MM;
            }
            else {
               menu = menu & 0xf0;
            }
         }
      }

      if ( ( menu == MM ) || ( menu == CP ) || ( menu == DP ) ||
           ( menu == MBR ) ) {
         ;
      }
      else {
         break;
      }
   }

   return ( menu );
}
