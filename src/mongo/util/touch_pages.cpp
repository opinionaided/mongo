/*    Copyright 2009 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */


#include "pch.h"

#include "mongo/util/touch_pages.h"

#include <fcntl.h>
#include <list>
#include <string>

#include "mongo/db/curop.h"
#include "mongo/db/pdfile.h"
#include "mongo/db/database.h"
#include "mongo/util/mmap.h"
#include "mongo/util/progress_meter.h"

namespace mongo {
    struct touch_location {
        int fd;
        off64_t offset;
        size_t length;
        Extent *ext;
    };
        
    void touchNs( const std::string& ns ) { 
        std::vector< touch_location > ranges;
        Client::ReadContext ctx(ns);
        {

            NamespaceDetails *nsd = nsdetails(ns.c_str());
            uassert( 16154, "namespace does not exist", nsd );
            
            for( DiskLoc L = nsd->firstExtent; !L.isNull(); L = L.ext()->xnext )  {
                MongoDataFile* mdf = cc().database()->getFile( L.a() );
                massert( 16238, "can't fetch extent file structure", mdf );
                touch_location tl;
                tl.fd = mdf->getFd();
                tl.offset = L.getOfs();
                tl.ext = L.ext();
                tl.length = tl.ext->length;
                
                ranges.push_back(tl);                
            }

        }
        LockMongoFilesShared lk;
        Lock::TempRelease tr;
        std::string progress_msg = "touch " + ns + " extents";
        ProgressMeterHolder pm( cc().curop()->setMessage( progress_msg.c_str() , ranges.size() ) );
        for ( std::vector< touch_location >::iterator it = ranges.begin(); it != ranges.end(); ++it ) {
            touch_pages( it->fd, it->offset, it->length, it->ext );
            pm.hit();
            killCurrentOp.checkForInterrupt(false);
        }
        pm.finished();
    }

#if defined(__linux__)    
    void touch_pages( int fd, off64_t offset, size_t length, Extent* ext ) {
        if ( -1 == readahead(fd, offset, length) ) {
            massert( 16237, str::stream() << "readahead failed on fd " << fd 
                     << " offset " << offset << " len " << length 
                     << " : " << errnoWithDescription(errno), 0 );
        }
    }
#else // if defined __linux__
    char _touch_pages_char_reader; // goes in .bss
    void touch_pages( int fd, off64_t offset, size_t length, Extent* ext ) {
        // read first byte of every page, in order
        const char *p = static_cast<const char *> (ext);
        for( int i = 0; i < length; i += 4096 /*pagesize*/ ) { 
            _touch_pages_char_reader += p[i];
        }
  
    }
#endif // if defined __linux__
}
