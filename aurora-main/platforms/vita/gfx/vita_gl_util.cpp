#include "vita_gl_util.hpp"
#if defined(__vita__)
#include <cstdio>
#include <string>
#include <vector>
namespace aurora::vita::gfx {
GLuint compile_shader(GLenum type,const char* src,std::string* diagnostics) noexcept {
  GLuint s=glCreateShader(type);
  if(!s){
    const char* stage=type==GL_VERTEX_SHADER?"vertex":type==GL_FRAGMENT_SHADER?"fragment":"unknown";
    std::printf("[aurora-vita] glCreateShader failed for %s shader\n",stage);
    if(diagnostics){diagnostics->append("glCreateShader failed for ");diagnostics->append(stage);diagnostics->append(" shader\n");}
    return 0;
  }
  glShaderSource(s,1,&src,nullptr); glCompileShader(s);
  GLint ok=0;
  glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok){
    GLint n=0;
    glGetShaderiv(s,GL_INFO_LOG_LENGTH,&n);
    std::vector<char> log(n>1?n:2);
    glGetShaderInfoLog(s,(GLsizei)log.size(),nullptr,log.data());
    const char* stage=type==GL_VERTEX_SHADER?"vertex":type==GL_FRAGMENT_SHADER?"fragment":"unknown";
    std::printf("[aurora-vita] %s shader compile failed: %s\n",stage,log.data());
    if(diagnostics){
      diagnostics->append(stage);
      diagnostics->append(" shader compile failed:\n");
      diagnostics->append(log.data());
      diagnostics->push_back('\n');
    }
    glDeleteShader(s);
    return 0;
  }
  return s;
}
GLuint link_program(const char* vs,const char* fs,std::string* diagnostics) noexcept {
  GLuint v=compile_shader(GL_VERTEX_SHADER,vs,diagnostics),f=compile_shader(GL_FRAGMENT_SHADER,fs,diagnostics);if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return 0;}
  GLuint p=glCreateProgram();
  if(!p){
    std::printf("[aurora-vita] glCreateProgram failed\n");
    if(diagnostics)diagnostics->append("glCreateProgram failed\n");
    glDeleteShader(v);glDeleteShader(f);return 0;
  }
  glAttachShader(p,v);glAttachShader(p,f);
  glBindAttribLocation(p,0,"a_position");glBindAttribLocation(p,1,"a_color0");glBindAttribLocation(p,2,"a_color1");
  for(unsigned i=0;i<8;i++){char n[16];std::snprintf(n,sizeof(n),"a_tex%u",i);glBindAttribLocation(p,3+i,n);}glLinkProgram(p);
  GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){GLint n=0;glGetProgramiv(p,GL_INFO_LOG_LENGTH,&n);std::vector<char> log(n>1?n:2);glGetProgramInfoLog(p,(GLsizei)log.size(),nullptr,log.data());std::printf("[aurora-vita] program link failed: %s\n",log.data());if(diagnostics){diagnostics->append("program link failed:\n");diagnostics->append(log.data());diagnostics->push_back('\n');}glDeleteProgram(p);p=0;}
  glDeleteShader(v);glDeleteShader(f);return p;
}
} // namespace aurora::vita::gfx
#endif
