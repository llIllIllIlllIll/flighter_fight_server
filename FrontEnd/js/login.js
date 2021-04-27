var app_login=new Vue({
    el:'#login',
    data:{
        accountname:"",
        pwd:""
    },
    methods:{
        login:function(){
            if(this.accountname==""||this.pwd=="")
            {
                alert("Enter Both Accountname And Password! ");
                return;
            }
            else{
                this.$http.post('http://localhost:8080/ebook/login',{accountname:this.accountname,pwd:this.pwd},
                {emulateJSON:true,withCredentials:true})
                .then(function(res){
                    //success with login request
                    console.log("请求成功");
                    this.a=res.body;
                    console.log(res);
                    if(this.a==true){
                        alert("You Have Been Successfully Logged In!");
                        //check is admin
                        this.$http.get('http://localhost:8080/ebook/isadmin',
                        {emulateJSON:true,withCredentials:true}).then(
                            function(res){
                                //success with isadmin request
                                if(res.body==true){
                                    window.location.href="admin-book-management.html";
                                }
                                else{
                                    window.location.href="index.html";
                                }
                            },
                            function(){
                                //otherwise
                                console.log("Cannot connect to http://localhost:8080/ebook/isadmin");
                            }
                        )        
                    }else{
                        alert("啊♂怎么回事");  
                    }
                },function(res){
                    //failure with login request
                    console.log(res);
                    if(res.status == 405)
                        alert("Your Account Name Or Password Is Not Correct!");
                    else if(res.status == 403)
                        alert("您的账号已经被禁用");
                });
            }
        }
        }
    }

)