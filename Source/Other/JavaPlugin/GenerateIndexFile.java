/*
	A very simple Java application used to create a sample index file (format version 6.05).
	This was only used when the Java Plugin cache exporter was in its early stages.
*/

import java.io.FileOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;

public class GenerateIndexFile {

	public static void main(String[] args) {
		
		if(args.length < 1 || args[0].isEmpty()) {
			System.out.println("Usage: java -cp \".\" GenerateIndexFile [Path To The Index File To Create]");
			System.exit(-1);
		}

		try {

			System.out.println("Generating the index file '" + args[0] + "'");
	        DataOutputStream stream = new DataOutputStream(new FileOutputStream(args[0]));  

			byte busy = 1;
			byte incomplete = 5;
			int cacheVersion = 605;
			byte isShortcutImage = 0;

			int contentLength = -200;
			long lastModified = 1598880569L * 1000;
			long expirationDate = 1598881670L * 1000;

			long _1 = 0L;
			byte _2 = 0;

			int section2Length = 2000;
			int section3Length = 3000;
			int section4Length = 4000;
			int section5Length = 5000;

			long _3 = 0L;
			long _4 = 0L;
			byte _5 = 0;

			int reducedManifestLength = 1234;
			int section4Pre15Length = 5678;
			byte hasOnlySignedEntries = 1;
			byte hasSingleCodeSource = 0;
			int section4CertsLength = 1234;
			int section4SignersLength = 5678;

			byte _6 = 0;
			long _7 = 0L;

			int reducedManifest2Length = 1234;
			byte isProxiedHost = 1;

			stream.writeByte(busy);
		    stream.writeByte(incomplete);
		    stream.writeInt(cacheVersion);
		    stream.writeByte(isShortcutImage);
		    stream.writeInt(contentLength);
		    stream.writeLong(lastModified);
		    stream.writeLong(expirationDate);
		    stream.writeLong(_1);
		    stream.writeByte(_2);
		    stream.writeInt(section2Length);
		    stream.writeInt(section3Length);
		    stream.writeInt(section4Length);
		    stream.writeInt(section5Length);
		    stream.writeLong(_3);
		    stream.writeLong(_4);
		    stream.writeByte(_5);
		    stream.writeInt(reducedManifestLength);
		    stream.writeInt(section4Pre15Length);
		    stream.writeByte(hasOnlySignedEntries);
		    stream.writeByte(hasSingleCodeSource);
		    stream.writeInt(section4CertsLength);
		    stream.writeInt(section4SignersLength);
		    stream.writeByte(_6);
		    stream.writeLong(_7);
		    stream.writeInt(reducedManifest2Length);
		    stream.writeByte(isProxiedHost);

		    // Pad to 128 bytes.
		    final int HEADER_SIZE = 128;
		    int numBytesWritten = stream.size();

			if(numBytesWritten < HEADER_SIZE)
		    {
				byte[] padding = new byte[HEADER_SIZE - numBytesWritten];
				stream.write(padding);
		    }

		    // str = "abc__123__íñ__xx__Ĉ߷__Ⅷꦅ__ガ䷀"
		    String str = "abc__123__\u00ED\u00F1__\u0108\u07F7__\u2167\uA985__\u30AC\u4DC0";
		    // 1 byte = \u0001 to \u007f
		    // 2 bytes = \u0080 to \u07FF
		    // 3 bytes = \u0800 to \uFFFF
		    stream.writeUTF(str); // Version
		    stream.writeUTF(str); // URL
		    stream.writeUTF(str); // Namespace ID
		    stream.writeUTF(str); // Codebase IP

			stream.close();

		} catch(IOException e) {
			System.out.println("An error occurred while writing to the output stream: " + e);
		}

		System.out.println("Finished running.");

	}

}
