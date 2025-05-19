#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "kernel/stdio.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define FILE_LIMITS 128				/* You may impose a limit of 128 open files per process, if necessary. */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// printf("System Call!\n");
	// thread_exit();
	int number = (int)f->R.rax;
	switch(number) {
		case SYS_HALT:                   /* Halt the operating system. */
			halt();
			break;
		case SYS_EXIT:                   /* Terminate this process. */
			exit(f->R.rdi);
			break;
		case SYS_FORK:                   /* Clone current process. */
			f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:                   /* Switch current process. */
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:                   /* Wait for a child process to die. */
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:                 /* Create a file. */
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:                 /* Delete a file. */
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:                   /* Open a file. */
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:               /* Obtain a file's size. */
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:                   /* Read from a file. */
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:                  /* Write to a file. */
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:                   /* Change position in a file. */
			break;
		case SYS_TELL:                   /* Report current position in a file. */
			break;
		case SYS_CLOSE:                  /* Close a file. */
			break;
		default:
			printf("undefined system call(%d)\n", number);
			exit(1);
	}
}

/* Shutdown pintos */
void halt (void) {
	/* power_off()을 호출해서 핀토스를 종료한다.
	발생 가능한 데드락 상황 등과 관련된 정보를 잃을 수 있기 때문에 거의 사용하지 않는 편이 좋다. */
	power_off();
	NOT_REACHED ();
}

/* Exit process */
void exit(int status) {
    /* 현재 실행중인 사용자 프로그램을 종료하고 커널에 status를 리턴한다.
	프로세스의 부모가 wait 중이라면(하단 참고), 그것을 리턴한다.
	관례 상 status는 성공하면 0을, 오류면 0이 아닌 값을 가진다. */
    printf("%s: exit(%d)\n" , thread_current()->name, status);
	thread_exit();
	NOT_REACHED ();
}

pid_t fork (const char *thread_name) {
	/* THREAD_NAME이라는 이름으로 현재 프로세스의 복사본을 생성한다.
	피호출자가 저장한 레지스터인 %RBX, %RSP, %RBP, %R12 - %R15는 반드시 그 값을 복사해야 하지만, 나머지는 그럴 필요는 없다.
	자식 프로세스의 pid를 리턴해야만 하며, 그렇지 않은 경우 유효한 pid를 가지면 안 된다.
	자식 프로세스에서는 리턴값이 0이어야 한다.
	자식 프로세스는 파일 디스크립터와 VM 공간 등을 포함해 복제된 리소스를 가져야 한다.
	부모 프로세스는 자식 프로세스가 성공적으로 복제된 것을 알기 전까지는 fork로부터 리턴하면 안 된다. 
	즉, 자식 프로세스가 자원을 복제하는 데 실패했다면, 부모의 fork() call은 TID_ERROR를 리턴해야 한다.
	템플릿은 대응하는 페이지 테이블 구조를 포함한 전체 유저 메모리 공간을 복사하는데 pml4_for_each()를 사용하지만, pte_for_each_func의 빠진 부분을 채워야 한다. */
}

/* Create child process and execute program corresponds to cmd_file on it */
int exec (const char *cmd_file) {
	/* 현재 프로세스를 cmd_line에 (어떤 인수와 함께) 주어진 이름의 실행 프로그램으로 바꾼다.
	성공하면 리턴하지 않지만, 어떤 이유로 프로그램을 로드하거나 실행할 수 없다면, 그 프로그램은 -1을 리턴하면서 종료된다.
	이 함수는 exec를 호출한 스레드의 이름을 바꾸지 않는다. 파일 디스크립터는 exec가 호출된 이후에도 열려있다는 것을 기억하자. */
	return process_exec(&cmd_file);
}

/* Wait for termination of child process whose process id is pid */
int wait (pid_t pid) {
	/* 자식 프로세스 pid를 기다리고, 자식의 종료 상태를 회수한다. pid가 아직 살아 있다면, 종료될 때까지 기다린다.
	pid가 exit하면서 넘긴 상태값을 리턴한다.
	pid가 exit()을 호출하지 않았지만 (exception 등에 의해) 커널에 의해 종료되었다면, wait(pid)는 -1을 리턴한다.
	부모 프로세스가 wait을 호출했을 때 이미 종료된 자식 프로세스를 기다릴 수도 있다. 
	하지만 커널은 여전히 부모가 자식의 종료 상태를 회수하거나 자식이 커널에 의해 종료되었음을 알도록 해야 한다.
	다음과 같은 조건들이 참인 경우 wait은 즉시 실패하고 -1을 리턴해야 한다:
		- pid가 호출한 프로세스의 직계 자식direct child을 참조하지 않는 경우:
			호출한 프로세스가 fork를 성공적으로 호출했을 때 pid를 리턴값으로 받을 때, pid는 호출한 프로세스의 직계 자식이다.
			A가 자식 B를 만들고, B가 자식 C를 만들면, A는 B가 죽어도 C를 wait할 수 없다는 점에서 자식은 상속되지 않는다. A에 의한 wait(C)는 실패해야 한다.
			비슷하게, 고아 프로세스들은 그들의 부모 프로세스가 (새로운 부모를 할당하기 전에) 종료한다면 새로운 부모를 가질 수 없다.
		- wait을 호출하는 프로세스가 이미 pid에 wait을 호출한 경우:
			즉, 프로세스는 어떤 주어진 자식에 대해 최대 한 번만 wait할 수 있다.
	프로세스들은 아무 수의 자식을 만들고, 아무 순서로 wait하고, 심지어 그 자식들 중 일부 또는 전체를 기다리지 않고 종료될 수 있다. 
	구현할 때 가능한 모든 경우를 고려해야 한다.
	부모가 기다리든지 말든지, 자식이 부모 전/후로 종료되던지 간에, struct thread를 포함한 프로세스의 모든 자원들은 free되어야 한다.
	최초의 프로세가 exit할 때까지는 핀토스가 종료되서는 안 된다. 제공된 핀토스는 main()에서 process_wait()를 호출해서 이 조건을 지키려고 한다. 
	process_wait()를 먼저 구현하고, process_wait()에 따라 wait 시스템 콜을 구현해 보자. */
	return process_wait(pid);
}

/* Create file which have size of initial_size. */
bool create (const char *file, unsigned initial_size) {
	/* initial_size 크기의 file이라는 파일을 생성한다. 성공하면 true, 실패하면 false를 반환한다. */
	return filesys_create(&file, initial_size);
}

/* Remove file whose name is file. */
bool remove (const char *file) {
	/* file이라는 파일을 삭제한다. 성공하면 true, 실패하면 false를 반환한다.
	파일이 열려 있는지 아닌지와는 상관없이 파일이 삭제되고, 열린 파일을 삭제하는 것이 그것을 닫지는 않는다. */
	return filesys_remove(&file);
}

/* Open the file corresponds to path in "file". */
int open (const char *file) {
	/* file이라는 파일을 연다. 성공하면 0 이상의 파일 디스크립터(fd) 값, 실패하면 -1을 리턴한다.
	파일 디스크립터 값 중 0은 표준 입력(STDIN_FILENO)에, 1은 표준 출력(STDOUT_FILENO)에 예약되어 있기 때문에 여기서는 0과 1을 리턴하지 않는다.
	각 프로세스는 파일 디스크립터의 독립적인 세트를 가지고 있고, 파일 디스크립터는 자식 프로세스로 상속된다.
	단일 프로세스로거나 다른 프로세스로거나에 관계 없이 단일 파일이 2번 이상 열리면 각각 새로운 파일 디스크립터를 리턴한다.
	단일 파일에 대한 다른 파일 디스크립터들은 각각 개별적인 close 호출에 의해 닫히고 서로 파일 위치를 공유하지 않는다.
	추가 작업을 하려면, 0부터 시작하는 정수를 리턴하는 Linux 방식을 따라야 한다. */
	int fd = filesys_open(&file);
	struct thread *curr = thread_current();
	curr->fdt[curr->next_fd++] = &file;
	return fd;
}

/* Return the size, in bytes, of the file open as fd. */
int filesize (int fd) {
	/* fd로 열린 파일의 사이즈를 바이트 단위로 리턴한다. */
	struct file *file = thread_current()->fdt[fd];
	return (int)file_length(file);
}

int read (int fd, void *buffer, unsigned length) {
	/* 열린 파일 fd로부터 size 바이트 만큼의 데이터를 buffer에 저장한다. 
	실제로 read한 바이트 수(EOFend-of-file에서는 0)를 리턴하거나, EOF가 아닌 다른 이유로 파일을 읽기 실패한 경우 -1을 리턴한다.
	fd 0은 input_getc()를 사용해 키보드로부터 읽는다. */
	if (fd == 0) {
		return input_getc();
	} else {
		struct file *file = thread_current()->fdt[fd];
		file_read(&file, &buffer, length);
	}
}

int write (int fd, const void *buffer, unsigned length) {
	/* buffer로부터 size 바이트 만큼의 데이터를 열린 파일 fd에 저장한다. 
	실제로 write한 바이트 수를 리턴한다 (실제로는 write되지 않는 바이트도 있어서 size보다 더 적을 수 있다).
	이전의 EOF에 쓰는 경우 일반적으로 파일을 확장하지만, 이 기능은 기본적인 파일 시스템에 구현되어 있지 않았다. 
	따라서 여기서는 EOF 전 까지 최대한 많이 쓰고 실제로 쓴 수를 리턴, 전혀 못 썼으면 0을 리턴한다.
	fd 1은 콘솔에 쓰는 것이다. 
	콘솔에 쓸 코드는 size가 수백 바이트를 넘지 않는 한 버퍼 전체를 한 번의 putbuf() 호출로 써야 한다(큰 버퍼를 나누는 것이 합리적이다). */
	if (fd == 1) {
		putbuf(&buffer, length);
		return length;
	} else {
		struct file *file = thread_current()->fdt[fd];
		return file_write(&file, &buffer, length);
	}
}

void seek (int fd, unsigned position) {
	/* 열린 파일 fd에서 position(파일 시작부터 떨어진 정도; 즉, position = 0은 파일의 시작)의 다음 바이트부터 읽거나 쓰도록 한다.
	현재 파일의 EOF를 지나 read하는 것은 오류가 아니다. (read는 0바이트(EOF)를 얻는다.)
	그러나 여기서는 핀토스의 파일이 고정된 길이를 가지기 때문에, EOF 이후의 write는 에러다.
	이러한 의미론은 파일 시스템에 구현되어 있기 때문에 시스템 콜 구현에서 다룰 필요는 없다. */
}

unsigned tell (int fd) {
	/* 열린 파일 fd에서 읽거나 쓸 다음 바이트의 위치(파일 시작부터 떨어진 정도)를 리턴한다. */
}

void close (int fd) {
	/* 파일 디스크립터 fd를 닫는다.
	프로세스를 exit하거나 종료하는 것은 (마치 각 파일에 대해 이 함수를 호출한 것 같이) 묵시적으로 모든 열린 파일을 닫는다. */
}