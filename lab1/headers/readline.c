//Function readline:
//Modified Dr.Carvers code
//  #include "readline.c"


#define FD_BUFFER_SIZE 100



char *readline(int fd)
{
	static char fd_buff[FD_BUFFER_SIZE+1];
	char next;
	int nread;
	int pos=0;
	while((nread=(read(fd,&next,1)))==1 && (pos < FD_BUFFER_SIZE)){
		fd_buff[pos]=next;
		if(next!='\n'){
			pos++;}
		else{
			break;}
	}
	if(nread==-1){
		return NULL;} 
	fd_buff[pos]='\0';
	if(pos==0){
		return NULL;}
	else{
	return fd_buff;}

}
