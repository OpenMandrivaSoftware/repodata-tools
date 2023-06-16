#include "Compression.h"
#include "Fd.h"

extern "C" {
#include <archive_entry.h>
#include <sys/stat.h>
#include <fcntl.h>
}

// This must be in sync (same order, same number of entries)
// with enum Format
static constexpr struct {
	int const libarchive_format;
	char const * const extension;
} formats[] = {
	{ ARCHIVE_FILTER_GZIP, ".gz" },
	{ ARCHIVE_FILTER_BZIP2, ".bz2" },
	{ ARCHIVE_FILTER_COMPRESS, ".Z" },
	{ ARCHIVE_FILTER_LZMA, ".lzma" },
	{ ARCHIVE_FILTER_XZ, ".xz" },
	{ ARCHIVE_FILTER_LZIP, ".lz" },
	{ ARCHIVE_FILTER_LRZIP, ".lrz" },
	{ ARCHIVE_FILTER_LZOP, ".lzop" },
	{ ARCHIVE_FILTER_GRZIP, ".grz" },
	{ ARCHIVE_FILTER_LZ4, ".lz4" },
	{ ARCHIVE_FILTER_ZSTD, ".zstd" }
};

bool Compression::CompressFile(String const &source, Format c, String target) {
	Fd fd(open(source, O_RDONLY));
	if(fd == -1)
		return false;

	archive *a = archive_write_new();
	if(!a)
		return false;
	archive_write_add_filter(a, formats[static_cast<int>(c)].libarchive_format);
	archive_write_set_format(a, ARCHIVE_FORMAT_RAW);

	if(!target)
		target = source + formats[static_cast<int>(c)].extension;

	if(archive_write_open_filename(a, target) == ARCHIVE_FATAL)
		return false;

	archive_entry *e = archive_entry_new();
	if(!e)
		return false;

	struct stat s;
	if(stat(source, &s) == -1 || !S_ISREG(s.st_mode))
		return false;

	archive_entry_set_pathname(e, source);
	archive_entry_set_size(e, s.st_size);
	archive_entry_set_filetype(e, AE_IFREG);
	archive_entry_set_perm(e, s.st_mode);
	archive_write_header(a, e);
	posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
	char buf[8192];
	while(int len = read(fd, buf, sizeof(buf))) {
		if(len < 0)
			return false;
		archive_write_data(a, buf, len);
	}
	archive_entry_free(e);
	archive_write_close(a);
	archive_write_free(a);
	return true;
}
