#include "http_conn.h"



void http_conn::init(int sockfd, const sockaddr_in &addr, connection_pool *conn_pool)
{
    //存socket地址,调用私有init
    m_address = addr;
    m_sockfd_ = sockfd;
    m_conn_pool_ = conn_pool;
    init();//初始化
}

void http_conn::init()
{
    m_read_idx_ = 0;
    m_write_idx_ = 0;
    //清空读缓冲区和写缓冲区
    memset(m_read_buf_, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file_, '\0', FILENAME_LEN);
}

bool http_conn::read_once()
{
    if(m_read_idx_ >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;
    while(1){
        bytes_read = recv(m_sockfd_, m_read_buf_ + m_read_idx_, 
                            READ_BUFFER_SIZE - m_read_idx_, 0);
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)//读完了
            {
                break;
            }
            return false;
        }
        else if(bytes_read == 0)
        {
            return false;
        }
        m_read_idx_ += bytes_read;
    }
    return true;
}






