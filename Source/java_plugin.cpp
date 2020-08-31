#include "web_cache_exporter.h"
#include "java_plugin.h"

/*
	@Format: IDX files.

	@ByteOrder: Big Endian

	Primitive Types in Java:
	- byte = 1 byte = s8
	- char = 2 bytes = u16
	- short = 2 bytes = s16
	- int = 4 bytes = s32
	- long = 8 bytes = s64

	@Decompiled: "com.sun.deploy.cache.CacheEntry" from "jre\lib\deploy.jar" in JDK 8 update 181

	int INCOMPLETE_FALSE = 0;
	int INCOMPLETE_TRUE = 1;
	int INCOMPLETE_ONHOLD = 2;

	int BUSY_FALSE = 0;
	int BUSY_TRUE = 1;

	readIndexFile(boolean paramBoolean)
	{
		RandomAccessFile file = this.openLockIndexFile("r", false);
		byte[] section1 = new byte[32];
		int numBytesRead = file.read(section1);
		DataInputStream stream = new DataInputStream(new ByteArrayInputStream(section1, 0, numBytesRead));

		byte busy = stream1.readByte(); // @Struct: s8

		// Missing respective cached file.
		int incomplete = stream1.readByte(); // @Struct: s8
		if(paramBoolean && incomplete == INCOMPLETE_TRUE)
		{
			incomplete = INCOMPLETE_ONHOLD;
		}

		int cacheVersion = stream1.readInt(); // @Struct: s32
		// Upgrade the index file if an older file format is detected.
		// @TODO: Document this.
		if(cacheVersion != Cache.getCacheVersion())
		{
			this.readIndexFileOld(stream, file);
        	this.saveUpgrade();
        	return;
		}

		readSection1Remaining(stream); // @Struct: Next 95 bytes.
		{
			byte isShortcutImage = stream1.readBytes(); // @Struct: s8
			int contentLength = stream1.readInt(); // @Struct: s32
			long lastModified = stream1.readLong(); // @Struct: s64
			long expirationDate = stream1.readLong(); // @Struct: s64

			long <unused> = stream1.readLong(); // @Struct: s64
			byte <unused> = stream1.readByte(); // @Struct: s8

		    int section2Length = stream1.readInt(); // @Struct: s32
		    int section3Length = stream1.readInt(); // @Struct: s32
		    int section4Length = stream1.readInt(); // @Struct: s32
		    int section5Length = stream1.readInt(); // @Struct: s32
		    
		    long <unused> = stream1.readLong(); // @Struct: s64
		    long <unused> = stream1.readLong(); // @Struct: s64
		    byte <unused> = stream1.readByte(); // @Struct: s8

			int reducedManifestLength = stream1.readInt(); // @Struct: s32
			int section4Pre15Length = stream1.readInt(); // @Struct: s32

		    byte <unused> = stream1.readByte(); // @Struct: s8
		    byte <unused> = stream1.readByte(); // @Struct: s8

		    int section4CertsLength = stream1.readInt(); // @Struct: s32
		    int section4SignersLength = stream1.readInt(); // @Struct: s32
		    
		    byte <unused> = stream1.readByte(); // @Struct: s8
		    long <unused> = stream1.readLong(); // @Struct: s64
		    
		    int reducedManifest2Length = stream1.readInt(); // @Struct: s32
		    byte IsProxied = stream1.readByte(); // @Struct: s8
		}

        readSection2(file);
        {
			if(section2Length > 0)
			{
				byte[] section2 = new byte[section2Length];
				file.read(section2);
				DataInputStream stream2 = new DataInputStream(new ByteArrayInputStream(section2));

				// Strings are stored in modified UTF-8.
				// @Docs: See writeUTF() in java.io.DataOutputStream and java.io.DataOutput
				// --> https://docs.oracle.com/javase/7/docs/api/java/io/DataOutput.html#writeUTF(java.lang.String)

				String version = stream2.readUTF(); // @Struct: Variable Length
				String url = stream2.readUTF(); // @Struct: Variable Length
				String namespaceId = stream2.readUTF(); // @Struct: Variable Length
				String codebaseIp = stream2.readUTF(); // @Struct: Variable Length

				readHeaders(stream2);
				{
					int numHeaders = stream2.readInt(); // @Struct: s32
					for(int i = 0; i < numHeaders; ++i)
					{
						String headerKey = stream2.readUTF(); // @Struct: Variable Length
						String headerValue = stream2.readUTF(); // @Struct: Variable Length
						
						// <null> corresponds to the server response, i.e., the first line in the HTTP headers.
						if(headerKey.equals("<null>"))
						{
							headerKey = null;
						}
						
						this.headerFields.add(headerKey, headerValue);
					}
				}
			}
        }
	}

*/
