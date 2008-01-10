/**
 * The main program of partclone 
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * clone/restore partition to a image, device or stdout.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include <features.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>


/**
 * progress.h - only for progress bar
 */
#include "progress.h"

/**
 * partclone.h - include some structures like image_head, opt_cmd, ....
 *               and functions for main used.
 */
#include "partclone.h"

/**
 * Include different filesystem header depend on what flag you want.
 * If cflag is _EXTFS, output to extfsclone.
 * My goal is give different clone utils by makefile .
 */
#ifdef EXTFS
    #include "extfsclone.h"
    #define FS "EXTFS"
#elif REISERFS
    #include "reiserfsclone.h"
    #define FS "REISERFS"
#elif REISER4
    #include "reiser4clone.h"
    #define FS "REISER4"
#elif XFS
    #include "xfsclone.h"
    #define FS "XFS"
#elif HFSPLUS
    #include "hfsplusclone.h"
    #define FS "HFS Plus"
#endif

/**
 * main functiom - for colne or restore data
 */
int main(int argc, char **argv){ 

    char*		source;			/// source data
    char*		target;			/// target data
    char*		buffer;			/// buffer data for malloc used
    int			dfr, dfw;		/// file descriptor for source and target
    int			r_size, w_size;		/// read and write size
    unsigned long long	block_id, copied = 0;	/// block_id is every block in partition
						/// copied is copied block count
    off_t		offset = 0, sf = 0;	/// seek postition, lseek result
    int			start, res, stop;	/// start, range, stop number for progress bar
    unsigned long long	total_write = 0;	/// the copied size 
    char		bitmagic[8] = "BiTmAgIc";// only for check postition
    char		bitmagic_r[8];		/// read magic string from image
    int			cmp;			/// compare magic string
    char		*bitmap;		/// the point for bitmap data
    int			debug = 0;		/// debug or not
    unsigned long	crc = 0xffffffffL;	/// CRC32 check code for writint to image
    unsigned long	crc_ck = 0xffffffffL;	/// CRC32 check code for checking
    int			c_size;			/// CRC32 code size
    char*		crc_buffer;		/// buffer data for malloc crc code
    int			done = 0;
    int			s_count = 0;

    progress_bar	prog;			/// progress_bar structure defined in progress.h
    cmd_opt		opt;			/// cmd_opt structure defined in partclone.h
    image_head		image_hdr;		/// image_head structure defined in partclone.h

    /**
     * get option and assign to opt structure
     * check parameter and read from argv
     */
    parse_options(argc, argv, &opt);

    if (geteuid() != 0)
	log_mesg(0, 1, 1, debug, "You are not logged as root. You may have \"access denied\" errors when working.\n"); 
    else
	log_mesg(0, 0, 0, debug, "UID is root.\n");


    /**
     * open source and target 
     * clone mode, source is device and target is image file/stdout
     * restore mode, source is image file/stdin and target is device
     * dd mode, source is device and target is device !!not complete
     */
    source = opt.source;
    target = opt.target;
    dfr = open_source(source, &opt);
    dfw = open_target(target, &opt);

    /**
     * if "-d / --debug" given
     * open debug file in "/var/log/partclone.log" for log message 
     */
    debug = opt.debug;
    //if(opt.debug)
	open_log();

    /**
     * get partition information like super block, image_head, bitmap
     * from device or image file.
     */
    if (opt.clone){

	log_mesg(0, 0, 0, debug, "Initial image hdr: get Super Block from partition\n");
	
	/// get Super Block information from partition
        initial_image_hdr(source, &image_hdr);

	memcpy(image_hdr.version, IMAGE_VERSION, VERSION_SIZE);

	/// alloc a memory to restore bitmap
	bitmap = (char*)malloc(sizeof(char)*image_hdr.totalblock);
	
	log_mesg(0, 0, 0, debug, "initial main bitmap pointer %i\n", bitmap);
	log_mesg(0, 0, 0, debug, "Initial image hdr: read bitmap table\n");

	/// read and check bitmap from partition
	readbitmap(source, image_hdr, bitmap);

	log_mesg(0, 0, 0, debug, "check main bitmap pointer %i\n", bitmap);

	/*
	sf = lseek(dfw, 0, SEEK_SET);
	log_mesg(0, 0, 0, debug, "seek %lli for writing image_head\n",sf);
        if (sf == (off_t)-1)
	    log_mesg(0, 1, 1, debug, "seek set %lli\n", sf);
	*/

	// write image_head to image file
	w_size = write_all(&dfw, (char *)&image_hdr, sizeof(image_head), &opt);
	if(w_size == -1)
	   log_mesg(0, 1, 1, debug, "write image_hdr to image error\n");

	// write bitmap information to image file
    	w_size = write_all(&dfw, bitmap, sizeof(char)*image_hdr.totalblock, &opt);
	if(w_size == -1)
	    log_mesg(0, 1, 1, debug, "write bitmap to image error\n");

    } else if (opt.restore){

	log_mesg(0, 0, 0, debug, "restore image hdr: get image_head from image file\n");
        /// get image information from image file
	restore_image_hdr(&dfr, &opt, &image_hdr);

	/// alloc a memory to restore bitmap
	bitmap = (char*)malloc(sizeof(char)*image_hdr.totalblock);

	/// check the image magic
	if (memcmp(image_hdr.magic, IMAGE_MAGIC, IMAGE_MAGIC_SIZE) != 0)
	    log_mesg(0, 1, 1, debug, "This is nor partclone image.\n");

	/// check the file system
	if (memcmp(image_hdr.fs, FS, FS_MAGIC_SIZE) != 0)
	    log_mesg(0, 1, 1, debug, "The image file system error.\n");

	log_mesg(0, 0, 0, debug, "initial main bitmap pointer %lli\n", bitmap);
	log_mesg(0, 0, 0, debug, "Initial image hdr: read bitmap table\n");

	/// read and check bitmap from image file
	get_image_bitmap(&dfr, opt, image_hdr, bitmap);

	/// check the dest partition size.
	check_size(&dfw, image_hdr.device_size);

	log_mesg(0, 0, 0, debug, "check main bitmap pointer %i\n", bitmap);
    }

    log_mesg(0, 0, 0, debug, "print image_head\n");

    /// print image_head
    print_image_hdr_info(image_hdr, opt);
	
    /**
     * initial progress bar
     */
    start = 0;				/// start number of progress bar
    stop = (int)image_hdr.usedblocks;	/// get the end of progress number, only used block
    res = 100;				/// the end of progress number
    log_mesg(0, 0, 0, debug, "Initial Progress bar\n");
    /// Initial progress bar
    progress_init(&prog, start, stop, res, (int)image_hdr.block_size);
    copied = 1;				/// initial number is 1

    /**
     * start read and write data between device and image file
     */
    if (opt.clone) {

	w_size = write_all(&dfw, bitmagic, 8, &opt); /// write a magic string

	/*
	/// log the offset
        sf = lseek(dfw, 0, SEEK_CUR);
	log_mesg(0, 0, 0, debug, "seek %lli for writing data string\n",sf); 
	if (sf == (off_t)-1)
	    log_mesg(0, 1, 1, debug, "seek set %lli\n", sf);
	*/

	/// read data from the first block and log the offset
	sf = lseek(dfr, 0, SEEK_SET);
	log_mesg(0, 0, 0, debug, "seek %lli for reading data string\n",sf);
        if (sf == (off_t)-1)
	    log_mesg(0, 1, 1, debug, "seek set %lli\n", sf);

	log_mesg(0, 0, 0, debug, "Total block %i\n", image_hdr.totalblock);
//        log_mesg(0, 0, 0, debug, "blockid,\tbitmap,\tread,\twrite,\tsize,\tseek,\tcopied,\terror\n");

	/// start clone partition to image file
        for( block_id = 0; block_id < image_hdr.totalblock; block_id++ ){
	    
	    r_size = 0;
	    w_size = 0;
	    log_mesg(0, 0, 0, debug, "block_id=%lli, ",block_id);

	    if((image_hdr.totalblock - 1 ) == block_id) 
		done = 1;

	    if (bitmap[block_id] == 1){
		/// if the block is used

		log_mesg(0, 0, 0, debug, "bitmap=%i, ",bitmap[block_id]);

		offset = (off_t)(block_id * image_hdr.block_size);
		//sf = lseek(dfr, offset, SEEK_SET);
                //if (sf == (off_t)-1)
                //    log_mesg(0, 1, 1, debug, "seek error %lli errno=%i\n", (long long)offset, (int)errno);
        	buffer = (char*)malloc(image_hdr.block_size); ///alloc a memory to copy data
        	
		/// read data from source to buffer
		r_size = read_all(&dfr, buffer, image_hdr.block_size, &opt);
		log_mesg(0, 0, 0, debug, "bs=%i and r=%i, ",image_hdr.block_size, r_size);
		if (r_size != (int)image_hdr.block_size)
		    log_mesg(0, 1, 1, debug, "read error %i \n", r_size);
        	
		/// write buffer to target
		w_size = write_all(&dfw, buffer, image_hdr.block_size, &opt);
		log_mesg(0, 0, 0, debug, "bs=%i and w=%i, ",image_hdr.block_size, w_size);
		if (w_size != (int)image_hdr.block_size)
		    log_mesg(0, 1, 1, debug, "write error %i \n", w_size);

		/// generate crc32 code and write it.
        	crc_buffer = (char*)malloc(sizeof(unsigned long)); ///alloc a memory to copy data
		crc = crc32(crc, buffer, w_size);
		memcpy(crc_buffer, &crc, sizeof(unsigned long));
		c_size = write_all(&dfw, crc_buffer, sizeof(unsigned long), &opt);
        	
		/// free buffer
		free(buffer);
		free(crc_buffer);

		progress_update(&prog, copied, done);
        	
		copied++;					/// count copied block
		total_write += (unsigned long long)(w_size);	/// count copied size
		log_mesg(0, 0, 0, debug, "total=%lli, ", total_write);
		
		/// read or write error
		if (r_size != w_size)
		    log_mesg(0, 1, 1, debug, "read and write different\n");
            } else {
		/// if the block is not used, I just skip it.
        	sf = lseek(dfr, image_hdr.block_size, SEEK_CUR);
		log_mesg(0, 0, 0, debug, "skip seek=%lli, ",sf);
                if (sf == (off_t)-1)
                    log_mesg(0, 1, 1, debug, "clone seek error %lli errno=%i\n", (long long)offset, (int)errno);
	    
		s_count++;
		if ((s_count >=100) || (done == 1)){
		    progress_update(&prog, copied, done);
		    s_count = 0;
		}
	    }
	    log_mesg(0, 0, 0, debug, "end\n");
        } /// end of for    
	sync_data(dfw, &opt);	
    
    } else if (opt.restore) {

	/**
	 * read magic string from image file
	 * and check it.
	 */
	r_size = read_all(&dfr, bitmagic_r, 8, &opt); /// read a magic string
        cmp = memcmp(bitmagic, bitmagic_r, 8);
        if(cmp != 0)
	    log_mesg(0, 1, 1, debug, "bitmagic error %i\n", cmp);

	/// seek to the first
        sf = lseek(dfw, 0, SEEK_SET);
	log_mesg(0, 0, 0, debug, "seek %lli for writing dtat string\n",sf);
	if (sf == (off_t)-1)
	    log_mesg(0, 1, 1, debug, "seek set %lli\n", sf);
    
	/*
	/// log the offset
        sf = lseek(dfr, 0, SEEK_CUR);
	log_mesg(0, 0, 0, debug, "seek %lli for reading data string\n",sf);
	if (sf == (off_t)-1)
	    log_mesg(0, 1, 1, debug, "seek set %lli\n", sf);
	*/

	/// start restore image file to partition
        for( block_id = 0; block_id < image_hdr.totalblock; block_id++ ){

	r_size = 0;
	w_size = 0;
        log_mesg(0, 0, 0, debug, "block_id=%lli, ",block_id);

	if((block_id + 1) == image_hdr.totalblock) 
	    done = 1;

    	if (bitmap[block_id] == 1){ 
	    /// The block is used
	    log_mesg(0, 0, 0, debug, "bitmap=%i, ",bitmap[block_id]);

	    offset = (off_t)(block_id * image_hdr.block_size);
	    //sf = lseek(dfw, offset, SEEK_SET);
            //if (sf == (off_t)-1)
            //    log_mesg(0, 1, 1, debug, "seek error %lli errno=%i\n", (long long)offset, (int)errno);
	    buffer = (char*)malloc(image_hdr.block_size); ///alloc a memory to copy data
	    r_size = read_all(&dfr, buffer, image_hdr.block_size, &opt);
	    log_mesg(0, 0, 0, debug, "bs=%i and r=%i, ",image_hdr.block_size, r_size);
	    if (r_size <0)
		log_mesg(0, 1, 1, debug, "read errno = %i \n", errno);

	    /// write block from buffer to partition
	    w_size = write_all(&dfw, buffer, image_hdr.block_size, &opt);
	    log_mesg(0, 0, 0, debug, "bs=%i and w=%i, ",image_hdr.block_size, w_size);
	    if (w_size != (int)image_hdr.block_size)
		log_mesg(0, 1, 1, debug, "write error %i \n", w_size);

	    /// read crc32 code and check it.
	    crc_ck = crc32(crc_ck, buffer, r_size);
            crc_buffer = (char*)malloc(sizeof(unsigned long)); ///alloc a memory to copy data
	    c_size = read_all(&dfr, crc_buffer, sizeof(unsigned long), &opt);
	    memcpy(&crc, crc_buffer, sizeof(unsigned long));
	    if (memcmp(&crc, &crc_ck, sizeof(unsigned long)) != 0)
		log_mesg(0, 1, 1, debug, "CRC Check  error\n OrigCRC:0x%08lX, DestCRC:0x%08lX", crc, crc_ck);

	    /// free buffer
	    free(buffer);
	    free(crc_buffer);

	    progress_update(&prog, copied, done);

       	    copied++;					/// count copied block
	    total_write += (unsigned long long) w_size;	/// count copied size

	    /// read or write error
	    //if ((r_size != w_size) || (r_size != image_hdr.block_size))
	    //	log_mesg(0, 1, 1, debug, "read and write different\n");
       	} else {

	    /// if the block is not used, I just skip it.
	    sf = lseek(dfw, image_hdr.block_size, SEEK_CUR);
	    log_mesg(0, 0, 0, debug, "seek=%lli, ",sf);
	    if (sf == (off_t)-1)
		log_mesg(0, 1, 1, debug, "seek error %lli errno=%i\n", (long long)offset, (int)errno);
	    s_count++;
	    if ((s_count >=100) || (done == 1)){
		progress_update(&prog, copied, done);
		s_count = 0;
	    }
	}
	log_mesg(0, 0, 0, debug, "end\n");

    	} // end of for
	sync_data(dfw, &opt);	
    }

    print_finish_info(opt);

    close (dfr);    /// close source
    close (dfw);    /// close target
    free(bitmap);   /// free bitmp
    if(opt.debug)
	close_log();
    return 0;	    /// finish
}
