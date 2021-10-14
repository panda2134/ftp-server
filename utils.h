//
// Created by panda2134 on 2021/10/15.
//

#ifndef FTP_SERVER_UTILS_H
#define FTP_SERVER_UTILS_H

/**
 * encode the current directory string into a form that's suitable for ftp
 * @param pathname the original pathname terminated by \000
 * @param dest destination of the encoded bytes
 * @param len length of encoded bytes
 */
void encode_pwd(const char* pathname, char* dest, size_t* result_len);

#endif //FTP_SERVER_UTILS_H
