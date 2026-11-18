#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
#define RTC_NOINIT_ATTR
using TickType_t=uint32_t;
#define pdMS_TO_TICKS(ms) (ms)
constexpr int HIGH=1,LOW=0;
constexpr int ESP_OK=0,ESP_ERR_TIMEOUT=1,ESP_ERR_INVALID_STATE=2;
constexpr int ESP_RST_POWERON=1,ESP_RST_SW=3;
constexpr int MALLOC_CAP_DMA=1,MALLOC_CAP_INTERNAL=2;
constexpr int SPI3_HOST=2,SPI_DMA_CH_AUTO=3;
constexpr int SPI_DEVICE_HALFDUPLEX=1,SPI_TRANS_USE_TXDATA=8;
using esp_err_t=int;
struct spi_bus_config_t { int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,data4_io_num,data5_io_num,data6_io_num,data7_io_num,max_transfer_sz; };
struct spi_device_interface_config_t { int clock_speed_hz,mode,spics_io_num,flags,queue_size; };
struct spi_transaction_t { uint32_t flags; size_t length; union { const void *tx_buffer; uint8_t tx_data[4]; }; };
using spi_device_handle_t=void*;
struct SPIClass { int ends=0,begins=0; int sclk=-1,mosi=-1; void end(){++ends;} void begin(int c,int miso,int m,int ss) { if(miso!=-1||ss!=-1)throw std::runtime_error("legacy pin configuration");++begins;sclk=c;mosi=m;} };
struct SerialStub { void println(const char*){} template<class...T>void printf(const char*,T...){} } Serial;
struct Restart {};
static std::map<int,int> pins;
static bool allocationFail=false;
static int busError=0,deviceError=0,queueError=0,resultError=0,resetReason=ESP_RST_POWERON;
static int busInitializations=0,busFrees=0,deviceAdds=0,frees=0,resultCalls=0;
static spi_transaction_t *pending=nullptr;
static spi_bus_config_t busConfig;
static spi_device_interface_config_t deviceConfig;
struct Packet {int cs,dc; std::vector<uint8_t> bytes;};
static std::vector<Packet> packets;
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
void digitalWrite(int pin,int value){pins[pin]=value;}
int esp_reset_reason(){return resetReason;}
void esp_restart(){throw Restart();}
void *heap_caps_malloc(size_t bytes,int caps){check(bytes==4096&&caps==(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL),"DMA allocation contract");return allocationFail?nullptr:std::malloc(bytes);}
void heap_caps_free(void*p){check(!pending,"free while DMA in flight");++frees;std::free(p);}
esp_err_t spi_bus_initialize(int host,const spi_bus_config_t *c,int dma){check(host==SPI3_HOST&&dma==SPI_DMA_CH_AUTO,"SPI3 DMA ownership");++busInitializations;busConfig=*c;return busError;}
esp_err_t spi_bus_free(int host){check(host==SPI3_HOST&&!pending,"bus free safety");++busFrees;return ESP_OK;}
esp_err_t spi_bus_add_device(int host,const spi_device_interface_config_t*c,spi_device_handle_t*h){check(host==SPI3_HOST,"device host");++deviceAdds;deviceConfig=*c;if(!deviceError)*h=reinterpret_cast<void*>(1);return deviceError;}
esp_err_t spi_device_queue_trans(spi_device_handle_t device,spi_transaction_t*t,TickType_t ticks){check(device&&ticks==1000&&!pending,"bounded single outstanding DMA");if(queueError)return queueError;pending=t;return ESP_OK;}
esp_err_t spi_device_get_trans_result(spi_device_handle_t device,spi_transaction_t**done,TickType_t ticks){check(device&&ticks==1000&&pending,"completion ownership");++resultCalls;if(resultError&&resultCalls==6)return resultError;check((pins[7]==LOW)!=(pins[16]==LOW),"one CS selected");int cs=pins[7]==LOW?7:16;int dc=pins[cs==7?6:18];const uint8_t*data=(pending->flags&SPI_TRANS_USE_TXDATA)?pending->tx_data:static_cast<const uint8_t*>(pending->tx_buffer);check(pending->length%8==0&&pending->length/8<=4096,"DMA packet bounds");packets.push_back({cs,dc,{data,data+pending->length/8}});*done=pending;pending=nullptr;return ESP_OK;}
#include "../../dma_display.h"

void reset() {pins.clear();packets.clear();pending=nullptr;allocationFail=false;busError=deviceError=queueError=resultError=0;resetReason=ESP_RST_POWERON;busInitializations=busFrees=deviceAdds=frees=resultCalls=0;}
TandoDmaDisplay display(){return TandoDmaDisplay(4,5,7,16,6,18);}
void wireCase(bool left,unsigned width,unsigned height,unsigned stride){
 reset();SPIClass legacy;auto d=display();check(d.begin(legacy),"DMA enabled");check(legacy.ends==1&&legacy.begins==0,"legacy handoff");check(busInitializations==1&&deviceAdds==1,"single bus/device");check(busConfig.mosi_io_num==5&&busConfig.sclk_io_num==4&&busConfig.miso_io_num==-1&&busConfig.max_transfer_sz==4096,"bus pins/buffer");check(busConfig.quadwp_io_num==-1&&busConfig.quadhd_io_num==-1&&busConfig.data4_io_num==-1&&busConfig.data5_io_num==-1&&busConfig.data6_io_num==-1&&busConfig.data7_io_num==-1,"unused bus pins disconnected");check(deviceConfig.clock_speed_hz==40000000&&deviceConfig.mode==0&&deviceConfig.spics_io_num==-1&&deviceConfig.queue_size==1,"device configuration");
 std::vector<uint16_t> frame(height*stride+7,0xdead);
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)frame[3+y*stride+x]=uint16_t((y*997+x*37)^0x1357);
 auto original=frame;
 check(d.writeRegion(left,frame.data()+3,stride,20,20,width,height),"region success");
 check(frame==original,"source untouched");check(packets.size()>=6,"commands and pixels");int cs=left?7:16;
 const unsigned endX=20+width-1,endY=20+height-1;
 std::vector<std::vector<uint8_t>> commands={{0x2a},{0,20,uint8_t(endX>>8),uint8_t(endX)},{0x2b},{0,20,uint8_t(endY>>8),uint8_t(endY)},{0x2c}};
 for(unsigned i=0;i<5;i++){check(packets[i].bytes==commands[i],"CASET/RASET/RAMWR address bytes");check(packets[i].cs==cs&&packets[i].dc==(i%2?HIGH:LOW),"command DC/CS");}
 std::vector<uint8_t> actual,expected;
 for(size_t i=5;i<packets.size();i++){check(packets[i].cs==cs&&packets[i].dc==HIGH,"pixel DC/CS");actual.insert(actual.end(),packets[i].bytes.begin(),packets[i].bytes.end());}
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){uint16_t pixel=frame[3+y*stride+x];expected.push_back(pixel>>8);expected.push_back(pixel);}
 check(actual==expected,"exact RGB565 byte order and strided packing");check(packets.size()==5+(expected.size()+4095)/4096,"bulk transaction budget");check(pins[7]==HIGH&&pins[16]==HIGH&&!pending,"completed transaction deasserts CS");
 size_t before=packets.size();check(!d.writeRegion(left,frame.data(),stride,240,20,1,1),"out of screen rejected");check(!d.writeRegion(left,frame.data(),width-1,20,20,width,height),"short stride rejected");check(!d.writeRegion(left,nullptr,stride,20,20,width,height),"null source rejected");check(packets.size()==before,"invalid regions emit no bus writes");
}
int main(){try{
 for(bool left:{false,true})for(unsigned width:{1u,2u,17u,199u,200u})for(unsigned height:{1u,3u,17u,200u})wireCase(left,width,height,width+3);
 for(int failure=0;failure<3;failure++){reset();SPIClass legacy;auto d=display();allocationFail=failure==0;busError=failure==1?1:0;deviceError=failure==2?1:0;check(!d.begin(legacy)&&!d.active(),"startup fallback");check(legacy.ends==(failure?1:0)&&legacy.begins==(failure?1:0),"legacy restored only after handoff");check(busFrees==(failure==2?1:0),"only owned bus freed");}
 for(bool timeout:{false,true}){reset();SPIClass legacy;auto d=display();check(d.begin(legacy),"fault fixture init");uint16_t pixels[4]={0x1234,0xabcd,0x0,0xffff};queueError=timeout?0:1;resultError=timeout?1:0;bool restarted=false;try{d.writeRegion(true,pixels,2,20,20,2,2);}catch(const Restart&){restarted=true;}check(restarted&&!d.active(),"runtime failure restarts without hanging");check(frees==0&&busFrees==0,"in-flight resources not freed");check((pending!=nullptr)==timeout,"completion timeout owns persistent transaction");if(timeout){check(!(pending->flags&SPI_TRANS_USE_TXDATA)&&pending->length==64,"timeout preserves actual pixel DMA");const uint8_t*live=static_cast<const uint8_t*>(pending->tx_buffer);check(live[0]==0x12&&live[1]==0x34&&live[2]==0xab&&live[3]==0xcd,"in-flight staging remains intact");}pending=nullptr;resetReason=ESP_RST_SW;queueError=resultError=0;SPIClass nextLegacy;auto next=display();int priorInit=busInitializations;check(!next.begin(nextLegacy)&&nextLegacy.ends==0&&busInitializations==priorInit,"software reset retains legacy fallback");resetReason=ESP_RST_POWERON;check(next.begin(nextLegacy),"power reset permits DMA retry");}
 std::puts("PASS DMA wire: 40 regions, exact pixels/addresses/CS, <=4096B chunks, allocation/init fallbacks, IRQ errors and soft-reset recovery");return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
